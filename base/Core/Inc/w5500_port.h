/**
  ******************************************************************************
  * @file    w5500_port.h
  * @brief   W5500 硬件移植层接口（SPI1 + CS_NET / RST / INT）
  *
  *          引脚（见 main.h）：
  *            SPI1 : SCK=PA5  MISO=PA6  MOSI=PA7
  *            CS   : PA4 (CS_NET，低有效)
  *            RST  : PA1 (低有效)
  *            INT  : PA0 (EXTI0，下降沿)
  ******************************************************************************
  */
#ifndef __W5500_PORT_H
#define __W5500_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 静态网络参数：网页测试页地址 http://192.168.97.138/ */
#define NET_MAC_ADDR  0x02, 0x08, 0xDC, 0x11, 0x22, 0x38
#define NET_IP_ADDR   192, 168, 97, 138
#define NET_MASK_ADDR 255, 255, 255, 0
#define NET_GW_ADDR   192, 168, 97, 1
#define NET_DNS_ADDR  114, 114, 114, 114

/**
  * @brief  复位 W5500、注册 ioLibrary 回调并写入静态网络参数
  * @retval 1 成功（VERSIONR == 0x04），0 失败
  */
uint8_t w5500_port_init(void);

/** @retval 1 网线已连接，0 未连接 */
uint8_t w5500_get_link(void);

/** @brief 读取并清除 INT 引脚事件标志 */
uint8_t w5500_int_event_take(void);

#ifdef __cplusplus
}
#endif

#endif /* __W5500_PORT_H */
