/**
  ******************************************************************************
  * @file    discovery.h
  * @brief   UDP 设备发现应答（socket1，端口 50000）
  *          收到 DISCOVER 广播后回 REPLY，携带本机 MAC / IP / 掩码 / 网关 / IP 模式。
  *          收到 SET 可配置静态参数或切换 DHCP，参数统一存于 netcfg。
  *          协议与上位机 host/device_finder.py 一致（大端）。
  ******************************************************************************
  */
#ifndef __DISCOVERY_H
#define __DISCOVERY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 复位内部状态（socket 由轮询中按需打开） */
void discovery_init(void);

/** @brief 在主循环中周期调用，处理 DISCOVER 并应答 */
void discovery_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __DISCOVERY_H */
