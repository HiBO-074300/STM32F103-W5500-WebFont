/**
  ******************************************************************************
  * @file    netcfg.c
  * @brief   W5500 网络参数统一状态源 + 轻量 DHCP 客户端（DORA）
  *          - 静态模式：IP / 掩码 / 网关由上位机 SET 下发，立即写入寄存器
  *          - DHCP 模式：先从 0.0.0.0:68 广播 DISCOVER/OFFER/REQUEST/ACK，成功后写入租约
  *          - REPLY / HTTP / JSON 统一从 netcfg_get() 读取，保证多端显示一致
  ******************************************************************************
  */
#include "netcfg.h"
#include "w5500_port.h"
#include "main.h"
#include "w5500.h"
#include "socket.h"

#include <string.h>

#define DHCP_SOCKET         2u
#define DHCP_CLIENT_PORT    68u
#define DHCP_SERVER_PORT    67u
#define DHCP_PKT_SIZE       320u
#define DHCP_TIMEOUT_MS     4000u
#define DHCP_RETRY_MAX      3u

#define DHCP_DISCOVER       1u
#define DHCP_OFFER          2u
#define DHCP_REQUEST        3u
#define DHCP_ACK            5u
#define DHCP_NAK            6u

#define DHCP_OPT_OFFSET     240u

typedef enum
{
    DHCP_IDLE = 0u,
    DHCP_START,
    DHCP_SELECTING,
    DHCP_REQUESTING,
    DHCP_BOUND,
    DHCP_FAILED
} dhcp_state_t;

static netcfg_t g_cfg;
static netcfg_t g_static_backup;
static dhcp_state_t g_dhcp_state = DHCP_IDLE;
static uint8_t g_dhcp_buf[DHCP_PKT_SIZE];
static uint8_t g_dhcp_xid[4];
static uint8_t g_dhcp_try;
static uint8_t g_dhcp_opened;
static uint32_t g_dhcp_tick;
static uint8_t g_offer_ip[4];
static uint8_t g_offer_sn[4];
static uint8_t g_offer_gw[4];
static uint8_t g_offer_sid[4];

static const uint8_t s_mac[6] = { NET_MAC_ADDR };
static const uint8_t s_zero[4] = { 0u, 0u, 0u, 0u };
static const uint8_t s_bcast[4] = { 255u, 255u, 255u, 255u };

/*============================ DHCP 报文构造 =================================*/
static uint16_t dhcp_build(uint8_t msg_type)
{
    uint8_t *p = g_dhcp_buf;
    uint16_t off;

    memset(p, 0, DHCP_PKT_SIZE);
    p[0] = 1u;                       /* BOOTREQUEST */
    p[1] = 1u;                       /* htype = Ethernet */
    p[2] = 6u;                       /* hlen */
    memcpy(&p[4], g_dhcp_xid, 4);
    memcpy(&p[28], s_mac, 6);
    p[236] = 0x63u;                  /* magic cookie */
    p[237] = 0x82u;
    p[238] = 0x53u;
    p[239] = 0x63u;

    off = DHCP_OPT_OFFSET;
    p[off++] = 53u; p[off++] = 1u; p[off++] = msg_type;

    p[off++] = 61u; p[off++] = 7u; p[off++] = 1u;   /* client ID = 01 + MAC */
    memcpy(&p[off], s_mac, 6);
    off += 6u;

    if (msg_type == DHCP_REQUEST)
    {
        p[off++] = 54u; p[off++] = 4u;              /* server identifier */
        memcpy(&p[off], g_offer_sid, 4);
        off += 4u;
        p[off++] = 50u; p[off++] = 4u;              /* requested IP */
        memcpy(&p[off], g_offer_ip, 4);
        off += 4u;
    }

    p[off++] = 55u; p[off++] = 3u;                  /* 请求掩码/网关/DNS */
    p[off++] = 1u; p[off++] = 3u; p[off++] = 6u;
    p[off++] = 255u;                                /* END */
    return off;
}

/** @return 1 解析成功；type/yiaddr/sn/gw/sid 由调用方提供缓冲 */
static uint8_t dhcp_parse(uint16_t len, uint8_t *type, uint8_t *yiaddr,
                          uint8_t *sn, uint8_t *gw, uint8_t *sid)
{
    const uint8_t *p = g_dhcp_buf;
    uint16_t off;

    if ((len < 240u) || (p[236] != 0x63u) || (p[237] != 0x82u) ||
        (p[238] != 0x53u) || (p[239] != 0x63u) ||
        (memcmp(&p[4], g_dhcp_xid, 4) != 0))
    {
        return 0u;
    }

    *type = 0u;
    memset(sn, 0, 4);
    memset(gw, 0, 4);
    memset(sid, 0, 4);
    memcpy(yiaddr, &p[16], 4);

    off = DHCP_OPT_OFFSET;
    while ((off + 2u) <= len)
    {
        uint8_t code = p[off++];
        uint8_t opt_len;

        if (code == 255u)
        {
            break;
        }
        if (code == 0u)
        {
            continue;
        }
        opt_len = p[off++];
        if ((opt_len == 0u) || ((uint16_t)(off + opt_len) > len))
        {
            break;
        }

        switch (code)
        {
            case 53u: *type = p[off]; break;
            case 1u:  if (opt_len >= 4u) memcpy(sn, &p[off], 4); break;
            case 3u:  if (opt_len >= 4u) memcpy(gw, &p[off], 4); break;
            case 54u: if (opt_len >= 4u) memcpy(sid, &p[off], 4); break;
            default: break;
        }
        off += opt_len;
    }
    return (*type != 0u) ? 1u : 0u;
}

static void dhcp_send(uint8_t msg_type)
{
    uint16_t len = dhcp_build(msg_type);
    (void)sendto(DHCP_SOCKET, g_dhcp_buf, len, (uint8_t *)s_bcast, DHCP_SERVER_PORT);
}

static void dhcp_start(void)
{
    uint32_t tick = HAL_GetTick();

    g_dhcp_xid[0] = (uint8_t)(tick >> 24);
    g_dhcp_xid[1] = (uint8_t)(tick >> 16);
    g_dhcp_xid[2] = (uint8_t)(tick >> 8);
    g_dhcp_xid[3] = (uint8_t)(tick);
    g_dhcp_try = 0u;
    g_dhcp_tick = tick;
    g_dhcp_state = DHCP_START;
}

static void dhcp_stop(void)
{
    if (g_dhcp_opened != 0u)
    {
        (void)close(DHCP_SOCKET);
        g_dhcp_opened = 0u;
    }
    g_dhcp_state = DHCP_IDLE;
}

static void dhcp_poll(void)
{
    uint8_t status;
    uint16_t ready;
    int32_t len;
    uint8_t peer_ip[4];
    uint16_t peer_port;
    uint8_t type;
    uint8_t yi[4], sn[4], gw[4], sid[4];
    uint32_t now = HAL_GetTick();

    if ((g_dhcp_state == DHCP_IDLE) || (g_dhcp_state == DHCP_BOUND) ||
        (g_dhcp_state == DHCP_FAILED))
    {
        return;
    }

    status = getSn_SR(DHCP_SOCKET);
    if (status == SOCK_CLOSED)
    {
        (void)socket(DHCP_SOCKET, Sn_MR_UDP, DHCP_CLIENT_PORT, 0u);
        g_dhcp_opened = 1u;
        return;
    }
    if (status != SOCK_UDP)
    {
        return;
    }

    if (g_dhcp_state == DHCP_START)
    {
        dhcp_send(DHCP_DISCOVER);
        g_dhcp_tick = now;
        g_dhcp_state = DHCP_SELECTING;
        return;
    }

    ready = getSn_RX_RSR(DHCP_SOCKET);
    if (ready > 0u)
    {
        if (ready > sizeof(g_dhcp_buf))
        {
            ready = sizeof(g_dhcp_buf);
        }
        len = recvfrom(DHCP_SOCKET, g_dhcp_buf, ready, peer_ip, &peer_port);
        if ((len > 0) && (dhcp_parse((uint16_t)len, &type, yi, sn, gw, sid) != 0u))
        {
            if ((g_dhcp_state == DHCP_SELECTING) && (type == DHCP_OFFER))
            {
                memcpy(g_offer_ip, yi, 4);
                memcpy(g_offer_sn, sn, 4);
                memcpy(g_offer_gw, gw, 4);
                memcpy(g_offer_sid, sid, 4);
                g_dhcp_try = 0u;
                g_dhcp_tick = now;
                dhcp_send(DHCP_REQUEST);
                g_dhcp_state = DHCP_REQUESTING;
                return;
            }
            if (g_dhcp_state == DHCP_REQUESTING)
            {
                if (type == DHCP_ACK)
                {
                    setSIPR(yi);
                    setSUBR(sn);
                    setGAR(gw);
                    memcpy(g_cfg.ip, yi, 4);
                    memcpy(g_cfg.sn, sn, 4);
                    memcpy(g_cfg.gw, gw, 4);
                    g_cfg.status = NETCFG_ST_READY;
                    dhcp_stop();
                    g_dhcp_state = DHCP_BOUND;
                    return;
                }
                if (type == DHCP_NAK)
                {
                    g_dhcp_state = DHCP_START;
                    return;
                }
            }
        }
    }

    if ((now - g_dhcp_tick) < DHCP_TIMEOUT_MS)
    {
        return;
    }

    /* 超时重试：SELECTING 重发 DISCOVER，REQUESTING 重发 REQUEST，超限回到 DISCOVER */
    if (g_dhcp_try < (DHCP_RETRY_MAX - 1u))
    {
        g_dhcp_try++;
        dhcp_send((g_dhcp_state == DHCP_SELECTING) ? DHCP_DISCOVER : DHCP_REQUEST);
    }
    else if (g_dhcp_state == DHCP_REQUESTING)
    {
        g_dhcp_state = DHCP_START;
        return;
    }
    else
    {
        g_cfg.status = NETCFG_ST_DHCP_FAIL;
        dhcp_stop();
        g_dhcp_state = DHCP_FAILED;
        return;
    }
    g_dhcp_tick = now;
}

/*============================= 对外接口 =====================================*/
void netcfg_init(void)
{
    const uint8_t dns[4] = { NET_DNS_ADDR };

    memset(&g_cfg, 0, sizeof(g_cfg));
    memset(&g_static_backup, 0, sizeof(g_static_backup));
    getSIPR(g_cfg.ip);
    getSUBR(g_cfg.sn);
    getGAR(g_cfg.gw);
    memcpy(g_cfg.dns, dns, 4);
    g_cfg.mode = NETCFG_MODE_STATIC;
    g_cfg.status = NETCFG_ST_READY;
    memcpy(&g_static_backup, &g_cfg, sizeof(g_static_backup));
}

void netcfg_poll(void)
{
    if (g_cfg.mode == NETCFG_MODE_DHCP)
    {
        dhcp_poll();
    }
}

const netcfg_t *netcfg_get(void)
{
    return &g_cfg;
}

uint8_t netcfg_apply(uint8_t mode, const uint8_t *ip,
                     const uint8_t *sn, const uint8_t *gw)
{
    if ((ip == NULL) || (sn == NULL) || (gw == NULL))
    {
        return 1u;
    }

    if (mode == NETCFG_MODE_DHCP)
    {
        memcpy(g_static_backup.ip, ip, 4);
        memcpy(g_static_backup.sn, sn, 4);
        memcpy(g_static_backup.gw, gw, 4);

        setSIPR((uint8_t *)s_zero);
        memcpy(g_cfg.ip, s_zero, 4);
        memcpy(g_cfg.sn, sn, 4);
        memcpy(g_cfg.gw, gw, 4);
        g_cfg.mode = NETCFG_MODE_DHCP;
        g_cfg.status = NETCFG_ST_DHCP_BUSY;
        dhcp_start();
    }
    else
    {
        dhcp_stop();
        setSIPR((uint8_t *)ip);
        setSUBR((uint8_t *)sn);
        setGAR((uint8_t *)gw);
        memcpy(g_cfg.ip, ip, 4);
        memcpy(g_cfg.sn, sn, 4);
        memcpy(g_cfg.gw, gw, 4);
        g_cfg.mode = NETCFG_MODE_STATIC;
        g_cfg.status = NETCFG_ST_READY;
    }
    return 0u;
}
