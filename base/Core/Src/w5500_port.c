/**
  ******************************************************************************
  * @file    w5500_port.c
  * @brief   W5500 (WIZnet ioLibrary) 硬件移植层
  *          实现 ioLibrary 所需的 CS / SPI / 临界区回调，并完成芯片初始化。
  ******************************************************************************
  */
#include "w5500_port.h"
#include "main.h"
#include "spi.h"
#include "wizchip_conf.h"
#include "w5500.h"

#include <string.h>

/* SPI 阻塞超时(ms) */
#define W5500_SPI_TIMEOUT   1000u
#define W5500_RST_LOW_MS    10u
#define W5500_RST_HIGH_MS   200u

static volatile uint8_t s_int_event;

/*============================ 片选 ==========================================*/
static void w5500_cs_select(void)
{
    HAL_GPIO_WritePin(CS_NET_GPIO_Port, CS_NET_Pin, GPIO_PIN_RESET);
}

static void w5500_cs_deselect(void)
{
    HAL_GPIO_WritePin(CS_NET_GPIO_Port, CS_NET_Pin, GPIO_PIN_SET);
}

/*========================= 临界区保护 =======================================*/
static uint32_t s_primask;

static void w5500_cris_enter(void)
{
    s_primask = __get_PRIMASK();
    __disable_irq();
}

static void w5500_cris_exit(void)
{
    if (s_primask == 0u)
    {
        __enable_irq();
    }
}

/*======================= SPI 单字节 / burst 读写 ============================*/
static uint8_t w5500_spi_readbyte(void)
{
    uint8_t tx = 0xFF;
    uint8_t rx = 0xFF;

    (void)HAL_SPI_TransmitReceive(&hspi1, &tx, &rx, 1, W5500_SPI_TIMEOUT);
    return rx;
}

static void w5500_spi_writebyte(uint8_t wb)
{
    (void)HAL_SPI_Transmit(&hspi1, &wb, 1, W5500_SPI_TIMEOUT);
}

static void w5500_spi_readburst(uint8_t *pBuf, uint16_t len)
{
    if ((pBuf != NULL) && (len != 0u))
    {
        (void)HAL_SPI_Receive(&hspi1, pBuf, len, W5500_SPI_TIMEOUT);
    }
}

static void w5500_spi_writeburst(uint8_t *pBuf, uint16_t len)
{
    if ((pBuf != NULL) && (len != 0u))
    {
        (void)HAL_SPI_Transmit(&hspi1, pBuf, len, W5500_SPI_TIMEOUT);
    }
}

/*============================ 硬件复位 ======================================*/
static void w5500_hw_reset(void)
{
    w5500_cs_deselect();

    HAL_GPIO_WritePin(RST_GPIO_Port, RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(W5500_RST_LOW_MS);
    HAL_GPIO_WritePin(RST_GPIO_Port, RST_Pin, GPIO_PIN_SET);
    HAL_Delay(W5500_RST_HIGH_MS);
}

/*============================ 移植层初始化 ==================================*/
uint8_t w5500_port_init(void)
{
    /* 每个 socket 的 TX/RX 各 2KB，8 个 socket 共 16KB */
    uint8_t txsize[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };
    uint8_t rxsize[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };
    const uint8_t mac[6] = { NET_MAC_ADDR };
    const uint8_t ip[4]  = { NET_IP_ADDR };
    const uint8_t sn[4]  = { NET_MASK_ADDR };
    const uint8_t gw[4]  = { NET_GW_ADDR };
    const uint8_t dns[4] = { NET_DNS_ADDR };
    wiz_NetInfo netinfo;

    w5500_hw_reset();

    reg_wizchip_cris_cbfunc(w5500_cris_enter, w5500_cris_exit);
    reg_wizchip_cs_cbfunc(w5500_cs_select, w5500_cs_deselect);
    reg_wizchip_spi_cbfunc(w5500_spi_readbyte, w5500_spi_writebyte);
    reg_wizchip_spiburst_cbfunc(w5500_spi_readburst, w5500_spi_writeburst);

    if (wizchip_init(txsize, rxsize) != 0)
    {
        return 0;
    }

    /* W5500 的 VERSIONR 固定为 0x04，用于确认 SPI 通信正常 */
    if (getVERSIONR() != 0x04u)
    {
        return 0;
    }

    memset(&netinfo, 0, sizeof(netinfo));
    memcpy(netinfo.mac, mac, 6);
    memcpy(netinfo.ip, ip, 4);
    memcpy(netinfo.sn, sn, 4);
    memcpy(netinfo.gw, gw, 4);
    memcpy(netinfo.dns, dns, 4);
    netinfo.dhcp = NETINFO_STATIC;
    wizchip_setnetinfo(&netinfo);

    /* 允许 socket 事件拉低 INTn，供 EXTI0 指示网络活动 */
    setSIMR(0xFF);

    return 1;
}

uint8_t w5500_get_link(void)
{
    return (wizphy_getphylink() == PHY_LINK_ON) ? 1u : 0u;
}

uint8_t w5500_int_event_take(void)
{
    uint8_t event = s_int_event;
    s_int_event = 0u;
    return event;
}

/* EXTI0 回调由 HAL_GPIO_EXTI_IRQHandler 调用 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == INT_Pin)
    {
        s_int_event = 1u;
    }
}
