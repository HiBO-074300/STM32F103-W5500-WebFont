/**
  ******************************************************************************
  * @file    discovery.c
  * @brief   UDP 设备发现应答（socket1 / 50000）
  *          协议与 host/device_finder.py 一致：
  *            数据区 26B: 'DVID' ver type session[4] target[6] src[6] seq[2] plen[2]
  *            校验区  2B: 数据区+payload 校验（大端）
  *                        v1 = 16 位累加和；v2 = CRC16-CCITT(0x1021, 初值 0xFFFF)
  *            REPLY payload 51B: name[32] ip[4] port[2] mode[1] fw[2] status[1]
  *                                + sn[4] gw[4] dhcp[1]（后 9B 为扩展，旧上位机只读前 42B）
  *            SET   payload  4B : ip[4]（旧格式，仅改 IP）
  *                            13B: dhcp[1] ip[4] sn[4] gw[4]（target 必须匹配本机 MAC）
  *            SET_NAME payload 1..32B: GBK 字节串（上位机按 GBK 编码，限长 32B）
  *            ACK   payload  1B : result[1]（0=成功）
  *            v2 起 REPLY/ACK 也走广播回包（255.255.255.255:源端口），便于跨网段救砖；
  *            应答版本跟随请求版本，v1 旧上位机仍可互通。
  ******************************************************************************
  */
#include "discovery.h"
#include "w5500_port.h"
#include "netcfg.h"
#include "main.h"
#include "socket.h"

#include <string.h>

#define DISCOVERY_SOCKET     1u
#define DISCOVERY_PORT       50000u
#define DISCOVERY_BUF_SIZE   128u

#define MSG_DISCOVER         1u
#define MSG_REPLY            2u
#define MSG_SET              3u
#define MSG_ACK              4u
#define MSG_SET_NAME         5u
#define DISC_VERSION_V1      1u
#define DISC_VERSION_V2      2u
#define DISC_VERSION         DISC_VERSION_V2

#define DISC_HEAD_LEN        26u
#define DISC_CRC_LEN         2u
#define DISC_PAYLOAD_LEN     42u
#define DISC_PAYLOAD_EXT     9u                       /* sn[4] gw[4] dhcp[1] */
#define DISC_REPLY_PAYLOAD   (DISC_PAYLOAD_LEN + DISC_PAYLOAD_EXT)
#define DISC_REPLY_LEN       (DISC_HEAD_LEN + DISC_CRC_LEN + DISC_REPLY_PAYLOAD)
#define DISC_SET_IP_LEN      4u
#define DISC_SET_NET_LEN     13u                      /* dhcp[1] ip[4] sn[4] gw[4] */
#define DISC_NAME_MAX        32u                      /* REPLY name[32]，GBK 字节上限 */
#define DISC_ACK_LEN         (DISC_HEAD_LEN + DISC_CRC_LEN + 1u)

#define DEV_MODE_HTTP        1u
#define DEV_STATUS_OK        0u
#define DEV_FW_VER           0x0103u
#define DEV_HTTP_PORT        80u

static const uint8_t s_magic[4] = { 'D', 'V', 'I', 'D' };
static uint8_t s_dev_name[DISC_NAME_MAX] = "STM32F103-W5500-WebFont";
static const uint8_t s_mac[6] = { NET_MAC_ADDR };
static const uint8_t s_bcast[4] = { 255u, 255u, 255u, 255u };

static uint8_t s_rx[DISCOVERY_BUF_SIZE];
static uint8_t s_tx[DISCOVERY_BUF_SIZE];
static uint8_t s_req_ver = DISC_VERSION_V1;   /* 当前请求版本，应答按同版本校验 */

/*========================== LED 诊断指示 ====================================*
 * 状态由 discovery_poll() 每周期刷新，非阻塞：
 *   心跳(1s 翻转)：程序在跑，无发现请求
 *   快闪(100ms)：收到 UDP 包但校验失败（广播到了，协议不匹配）
 *   常亮(1s)：收到合法 DISCOVER 且 REPLY 发送成功（链路全通）
 * 优先级：事件期间进入快闪/常亮，持续结束后自动回心跳。
 */
typedef enum
{
    LED_HEARTBEAT = 0u,
    LED_RX_FLASH,
    LED_REPLY_ON
} led_mode_t;

static led_mode_t s_led_mode = LED_HEARTBEAT;
static uint32_t s_led_until = 0u;
static uint32_t s_led_toggle = 0u;
static uint8_t s_led_state = 0u;

#define LED_HEARTBEAT_MS   1000u
#define LED_RX_FLASH_MS    100u
#define LED_RX_FLASH_HOLD  600u
#define LED_REPLY_HOLD     1000u

static void discovery_led_write(uint8_t on)
{
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin,
                      (on != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void discovery_led_enter(led_mode_t mode, uint32_t hold_ms)
{
    s_led_mode = mode;
    s_led_until = HAL_GetTick() + hold_ms;
    s_led_toggle = HAL_GetTick();
}

/** 收到任意 UDP 包：标记快闪（若随后校验通过会被 REPLY 常亮覆盖） */
static void discovery_led_rx(void)
{
    if (s_led_mode != LED_REPLY_ON)
    {
        discovery_led_enter(LED_RX_FLASH, LED_RX_FLASH_HOLD);
    }
}

/** REPLY 发送成功：标记常亮 */
static void discovery_led_reply(void)
{
    discovery_led_enter(LED_REPLY_ON, LED_REPLY_HOLD);
}

/** 主循环周期调用，刷新 LED 状态 */
static void discovery_led_poll(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t period;

    if ((s_led_mode != LED_HEARTBEAT) && ((now - s_led_until) < 0x7FFFFFFFu) &&
        (now >= s_led_until))
    {
        s_led_mode = LED_HEARTBEAT;
    }

    if (s_led_mode == LED_REPLY_ON)
    {
        discovery_led_write(1u);
        return;
    }

    period = (s_led_mode == LED_RX_FLASH) ? LED_RX_FLASH_MS : LED_HEARTBEAT_MS;
    if ((now - s_led_toggle) >= period)
    {
        s_led_toggle = now;
        s_led_state ^= 1u;
        discovery_led_write(s_led_state);
    }
}

/** v1 累加和：magic..plen + payload，与旧版上位机 _checksum 一致 */
static uint16_t discovery_checksum(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0u;
    uint16_t i;

    for (i = 0u; i < len; i++)
    {
        sum += data[i];
    }
    return (uint16_t)(sum & 0xFFFFu);
}

/** v2 CRC16-CCITT-FALSE：多项式 0x1021，初值由调用方给出（分段计算用） */
static uint16_t discovery_crc16(const uint8_t *data, uint16_t len, uint16_t crc)
{
    uint16_t i;
    uint8_t bit;

    for (i = 0u; i < len; i++)
    {
        crc ^= ((uint16_t)data[i] << 8);
        for (bit = 0u; bit < 8u; bit++)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/** 按版本计算整包校验：头(26B) 与 payload 分段，跳过中间 2B 校验字段 */
static uint16_t discovery_calc(uint8_t ver, const uint8_t *head,
                               uint16_t head_len, const uint8_t *payload,
                               uint16_t plen)
{
    if (ver == DISC_VERSION_V2)
    {
        uint16_t crc = discovery_crc16(head, head_len, 0xFFFFu);
        return discovery_crc16(payload, plen, crc);
    }
    return (uint16_t)(discovery_checksum(head, head_len) +
                      discovery_checksum(payload, plen));
}

static uint8_t discovery_is_broadcast(const uint8_t *mac)
{
    uint8_t i;

    for (i = 0u; i < 6u; i++)
    {
        if (mac[i] != 0xFFu)
        {
            return 0u;
        }
    }
    return 1u;
}

/**
 * 校验请求头：magic/版本/类型/长度/target/校验和。
 * @return 消息类型（MSG_DISCOVER / MSG_SET / MSG_SET_NAME），0 表示无效。
 *         DISCOVER 允许广播或指定本机 MAC；SET/SET_NAME 必须精确匹配本机 MAC。
 */
static uint8_t discovery_parse(const uint8_t *buf, uint16_t len)
{
    uint16_t plen;
    uint16_t cs_recv;
    uint16_t cs_calc;
    uint8_t ver;
    uint8_t type;

    if (len < (DISC_HEAD_LEN + DISC_CRC_LEN))
    {
        return 0u;
    }
    if (memcmp(buf, s_magic, 4) != 0)
    {
        return 0u;
    }
    ver = buf[4];
    if ((ver != DISC_VERSION_V1) && (ver != DISC_VERSION_V2))
    {
        return 0u;
    }

    type = buf[5];
    if ((type != MSG_DISCOVER) && (type != MSG_SET) && (type != MSG_SET_NAME))
    {
        return 0u;
    }

    plen = ((uint16_t)buf[24] << 8) | buf[25];
    if (len != (DISC_HEAD_LEN + DISC_CRC_LEN + plen))
    {
        return 0u;
    }
    if ((type == MSG_SET) && (plen != DISC_SET_IP_LEN) && (plen != DISC_SET_NET_LEN))
    {
        return 0u;
    }
    if ((type == MSG_SET_NAME) && ((plen == 0u) || (plen > DISC_NAME_MAX)))
    {
        return 0u;
    }

    cs_recv = ((uint16_t)buf[26] << 8) | buf[27];
    cs_calc = discovery_calc(ver, buf, DISC_HEAD_LEN,
                             buf + DISC_HEAD_LEN + DISC_CRC_LEN, plen);
    if (cs_recv != cs_calc)
    {
        return 0u;
    }

    if ((type == MSG_SET) || (type == MSG_SET_NAME))
    {
        if (memcmp(&buf[10], s_mac, 6) != 0)
        {
            return 0u;
        }
    }
    else if ((discovery_is_broadcast(&buf[10]) == 0u) &&
             (memcmp(&buf[10], s_mac, 6) != 0))
    {
        return 0u;
    }
    s_req_ver = ver;
    return type;
}

/** 由请求构造 REPLY：session/seq 原样带回，target 填请求方 src */
static void discovery_build_reply(const uint8_t *req)
{
    const netcfg_t *cfg;
    uint16_t cs;
    uint16_t off;

    memcpy(&s_tx[0], s_magic, 4);
    s_tx[4] = s_req_ver;
    s_tx[5] = MSG_REPLY;

    /* session（请求偏移 6..9）原样回传 */
    memcpy(&s_tx[6], &req[6], 4);

    /* target = 上位机标识（请求 src，偏移 16..21） */
    memcpy(&s_tx[10], &req[16], 6);

    /* src = 本机 MAC */
    memcpy(&s_tx[16], s_mac, 6);

    /* seq（请求偏移 22..23）原样回传 */
    memcpy(&s_tx[22], &req[22], 2);

    /* payload 长度，大端 51（前 42B 兼容旧上位机） */
    s_tx[24] = 0u;
    s_tx[25] = DISC_REPLY_PAYLOAD;

    /* payload 从偏移 28 开始；前 42B 格式不变 */
    off = DISC_HEAD_LEN + DISC_CRC_LEN;
    cfg = netcfg_get();
    memcpy(&s_tx[off], s_dev_name, 32);
    off += 32u;
    memcpy(&s_tx[off], cfg->ip, 4);
    off += 4u;
    s_tx[off++] = (uint8_t)(DEV_HTTP_PORT >> 8);
    s_tx[off++] = (uint8_t)(DEV_HTTP_PORT & 0xFFu);
    s_tx[off++] = DEV_MODE_HTTP;
    s_tx[off++] = (uint8_t)(DEV_FW_VER >> 8);
    s_tx[off++] = (uint8_t)(DEV_FW_VER & 0xFFu);
    s_tx[off++] = (cfg->status == NETCFG_ST_READY) ? DEV_STATUS_OK : cfg->status;

    /* 扩展 9B：子网掩码 / 网关 / IP 模式（0 静态 1 DHCP） */
    memcpy(&s_tx[off], cfg->sn, 4);
    off += 4u;
    memcpy(&s_tx[off], cfg->gw, 4);
    off += 4u;
    s_tx[off] = (cfg->mode == NETCFG_MODE_DHCP) ? 1u : 0u;

    cs = discovery_calc(s_req_ver, s_tx, DISC_HEAD_LEN,
                        &s_tx[DISC_HEAD_LEN + DISC_CRC_LEN], DISC_REPLY_PAYLOAD);
    s_tx[26] = (uint8_t)(cs >> 8);
    s_tx[27] = (uint8_t)(cs & 0xFFu);
}

/** 由 SET 请求构造 ACK：session/seq 原样带回，payload 为结果码 */
static void discovery_build_ack(const uint8_t *req, uint8_t result)
{
    uint16_t cs;

    memcpy(&s_tx[0], s_magic, 4);
    s_tx[4] = s_req_ver;
    s_tx[5] = MSG_ACK;
    memcpy(&s_tx[6], &req[6], 4);       /* session */
    memcpy(&s_tx[10], &req[16], 6);     /* target = 请求方标识 */
    memcpy(&s_tx[16], s_mac, 6);        /* src = 本机 MAC */
    memcpy(&s_tx[22], &req[22], 2);     /* seq */
    s_tx[24] = 0u;
    s_tx[25] = 1u;                      /* payload 长度 */
    s_tx[28] = result;

    cs = discovery_calc(s_req_ver, s_tx, DISC_HEAD_LEN, &s_tx[28], 1u);
    s_tx[26] = (uint8_t)(cs >> 8);
    s_tx[27] = (uint8_t)(cs & 0xFFu);
}

/** 应用网络参数：统一交给 netcfg，REPLY / HTTP / JSON 均从其快照读取 */
static void discovery_apply_net(const uint8_t *payload, uint16_t plen)
{
    const netcfg_t *cur = netcfg_get();
    uint8_t mode;
    uint8_t ip[4];
    uint8_t sn[4];
    uint8_t gw[4];

    if (plen == DISC_SET_NET_LEN)
    {
        mode = (payload[0] != 0u) ? NETCFG_MODE_DHCP : NETCFG_MODE_STATIC;
        memcpy(ip, &payload[1], 4);
        memcpy(sn, &payload[5], 4);
        memcpy(gw, &payload[9], 4);
    }
    else
    {
        /* 旧格式 4B：仅改 IP，沿用当前掩码 / 网关 / IP 模式 */
        mode = cur->mode;
        memcpy(ip, payload, 4);
        memcpy(sn, cur->sn, 4);
        memcpy(gw, cur->gw, 4);
    }
    (void)netcfg_apply(mode, ip, sn, gw);
}

/** 应用 GBK 设备名：固定 32B，不足补 0；仅 RAM 生效，掉电恢复默认名 */
static void discovery_apply_name(const uint8_t *name, uint16_t len)
{
    if ((name == NULL) || (len == 0u) || (len > DISC_NAME_MAX))
    {
        return;
    }
    memset(s_dev_name, 0, sizeof(s_dev_name));
    memcpy(s_dev_name, name, len);
}

void discovery_init(void)
{
    /* socket 在 poll 中按需打开，无需额外状态 */
}

void discovery_poll(void)
{
    uint8_t status;
    uint8_t msg_type;
    uint16_t ready;
    int32_t len;
    uint8_t peer_ip[4];
    uint16_t peer_port;

    discovery_led_poll();

    status = getSn_SR(DISCOVERY_SOCKET);
    if (status == SOCK_CLOSED)
    {
        (void)socket(DISCOVERY_SOCKET, Sn_MR_UDP, DISCOVERY_PORT, 0u);
        return;
    }
    if (status != SOCK_UDP)
    {
        return;
    }

    ready = getSn_RX_RSR(DISCOVERY_SOCKET);
    if (ready == 0u)
    {
        return;
    }
    if (ready > DISCOVERY_BUF_SIZE)
    {
        ready = DISCOVERY_BUF_SIZE;
    }

    len = recvfrom(DISCOVERY_SOCKET, s_rx, ready, peer_ip, &peer_port);
    (void)peer_ip;   /* ioLibrary 要求传入地址缓冲；v2 统一广播回包，源 IP 不再使用 */
    if (len <= 0)
    {
        return;
    }

    /* v2 双方均广播：忽略其它设备/上位机的 REPLY/ACK，避免 LED 误指示 */
    if ((len >= 6) && (memcmp(s_rx, s_magic, 4) == 0) &&
        ((s_rx[5] == MSG_REPLY) || (s_rx[5] == MSG_ACK)))
    {
        return;
    }

    discovery_led_rx();
    msg_type = discovery_parse(s_rx, (uint16_t)len);
    if (msg_type == MSG_DISCOVER)
    {
        discovery_build_reply(s_rx);
        if (sendto(DISCOVERY_SOCKET, s_tx, DISC_REPLY_LEN,
                   (uint8_t *)s_bcast, peer_port) > 0)
        {
            discovery_led_reply();
        }
    }
    else if (msg_type == MSG_SET)
    {
        uint16_t set_plen = ((uint16_t)s_rx[24] << 8) | s_rx[25];

        /* 先回 ACK 再应用参数：DHCP 切换会把 SIPR 置 0，先应答保证流程完整 */
        discovery_build_ack(s_rx, 0u);
        if (sendto(DISCOVERY_SOCKET, s_tx, DISC_ACK_LEN,
                   (uint8_t *)s_bcast, peer_port) > 0)
        {
            discovery_apply_net(&s_rx[28], set_plen);
            discovery_led_reply();
        }
    }
    else if (msg_type == MSG_SET_NAME)
    {
        uint16_t name_len = ((uint16_t)s_rx[24] << 8) | s_rx[25];

        discovery_build_ack(s_rx, 0u);
        if (sendto(DISCOVERY_SOCKET, s_tx, DISC_ACK_LEN,
                   (uint8_t *)s_bcast, peer_port) > 0)
        {
            discovery_apply_name(&s_rx[28], name_len);
            discovery_led_reply();
        }
    }
}
