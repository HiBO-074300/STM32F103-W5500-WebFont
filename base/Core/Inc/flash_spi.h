/**
  ******************************************************************************
  * @file    flash_spi.h
  * @brief   两片 SPI Flash 共用的底层 SPI（默认软件模拟，兼容原硬件 API）
  *
  *          引脚：PC13=MOSI  PC14=MISO  PC15=SCK
  *                PA12=F1_CS（普通 Flash）  PA15=F2_CS（字库 Flash）
  ******************************************************************************
  */
#ifndef __FLASH_SPI_H
#define __FLASH_SPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0=使用本文件的软件 SPI；1=使用原工程硬件 SPI（spi.h 中的 hspi1） */
#ifndef FONT_USE_HW_SPI
#define FONT_USE_HW_SPI 0
#endif

/** @brief 初始化（释放两片 Flash 的片选） */
void FLASH_SPI_Init(void);

/** @brief 选中普通 Flash(PA12)，同时释放字库 Flash */
void FLASH_SPI_SelectF1(void);

/** @brief 选中字库 Flash(PA15)，同时释放普通 Flash */
void FLASH_SPI_SelectF2(void);

/** @brief 释放两片 Flash */
void FLASH_SPI_Deselect(void);

#if (FONT_USE_HW_SPI == 0)
/* 与参考工程 Fontlib.c 中同名同签名，方便原代码直接移植 */
uint8_t SPI_ReadByte(void);
void SPI_WriteByte(uint8_t txData);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_SPI_H */
