/**
  ******************************************************************************
  * @file    netcfg.h
  * @brief   W5500 网络参数统一状态源：静态 IP / 子网掩码 / 网关 + DHCP 客户端
  *          REPLY、HTTP 页面与 JSON 接口均从本模块读取，避免各处硬编码。
  ******************************************************************************
  */
#ifndef __NETCFG_H
#define __NETCFG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NETCFG_MODE_STATIC      0u
#define NETCFG_MODE_DHCP        1u

#define NETCFG_ST_READY         0u   /* 静态参数已生效 / DHCP 已获取地址 */
#define NETCFG_ST_DHCP_BUSY     1u   /* DHCP 正在获取地址 */
#define NETCFG_ST_DHCP_FAIL     2u   /* DHCP 获取失败，IP 为 0.0.0.0 */

typedef struct
{
    uint8_t mode;                /* NETCFG_MODE_STATIC / NETCFG_MODE_DHCP */
    uint8_t status;              /* NETCFG_ST_* */
    uint8_t ip[4];
    uint8_t sn[4];
    uint8_t gw[4];
    uint8_t dns[4];
} netcfg_t;

/** @brief 复位状态；应在 w5500_port_init() 之后调用 */
void netcfg_init(void);

/** @brief 主循环周期调用，驱动 DHCP 状态机（静态模式为空操作） */
void netcfg_poll(void);

/** @brief 读取当前网络参数（只读快照） */
const netcfg_t *netcfg_get(void);

/**
  * @brief 应用网络参数
  * @param mode NETCFG_MODE_STATIC / NETCFG_MODE_DHCP
  * @param ip   静态 IP（DHCP 模式下保存为回退参数）
  * @param sn   子网掩码
  * @param gw   网关
  * @retval 0 成功
  */
uint8_t netcfg_apply(uint8_t mode, const uint8_t *ip,
                     const uint8_t *sn, const uint8_t *gw);

#ifdef __cplusplus
}
#endif

#endif /* __NETCFG_H */
