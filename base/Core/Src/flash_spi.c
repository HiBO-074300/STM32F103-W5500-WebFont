/**
  ******************************************************************************
  * @file    flash_spi.c
  * @brief   软件模拟 SPI（模式0），供两片 SPI Flash 共用
  ******************************************************************************
  */
#include "flash_spi.h"
#include "main.h"

#define FLASH_SPI_SCK_LOW()   HAL_GPIO_WritePin(FLASH_SCK_GPIO_Port,   FLASH_SCK_Pin,   GPIO_PIN_RESET)
#define FLASH_SPI_SCK_HIGH()  HAL_GPIO_WritePin(FLASH_SCK_GPIO_Port,   FLASH_SCK_Pin,   GPIO_PIN_SET)
#define FLASH_SPI_MOSI_LOW()  HAL_GPIO_WritePin(FALSH_MOSI_GPIO_Port,  FALSH_MOSI_Pin,  GPIO_PIN_RESET)
#define FLASH_SPI_MOSI_HIGH() HAL_GPIO_WritePin(FALSH_MOSI_GPIO_Port,  FALSH_MOSI_Pin,  GPIO_PIN_SET)
#define FLASH_SPI_MISO_READ() HAL_GPIO_ReadPin(FLASH_MISO_GPIO_Port,   FLASH_MISO_Pin)

#define FLASH_SPI_DELAY()     do { __NOP(); __NOP(); __NOP(); } while (0)

void FLASH_SPI_Init(void)
{
    FLASH_SPI_SCK_LOW();
    FLASH_SPI_MOSI_LOW();
    FLASH_SPI_Deselect();
}

void FLASH_SPI_SelectF1(void)
{
    HAL_GPIO_WritePin(F2_CS_GPIO_Port, F2_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(F1_CS_GPIO_Port, F1_CS_Pin, GPIO_PIN_RESET);
}

void FLASH_SPI_SelectF2(void)
{
    HAL_GPIO_WritePin(F1_CS_GPIO_Port, F1_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(F2_CS_GPIO_Port, F2_CS_Pin, GPIO_PIN_RESET);
}

void FLASH_SPI_Deselect(void)
{
    HAL_GPIO_WritePin(F1_CS_GPIO_Port, F1_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(F2_CS_GPIO_Port, F2_CS_Pin, GPIO_PIN_SET);
}

#if (FONT_USE_HW_SPI == 0)

uint8_t SPI_ReadByte(void)
{
    uint8_t data = 0;
    uint8_t i;

    for (i = 0; i < 8u; i++)
    {
        data <<= 1;
        FLASH_SPI_SCK_HIGH();
        FLASH_SPI_DELAY();
        if (FLASH_SPI_MISO_READ() == GPIO_PIN_SET)
        {
            data |= 0x01u;
        }
        FLASH_SPI_SCK_LOW();
        FLASH_SPI_DELAY();
    }
    return data;
}

void SPI_WriteByte(uint8_t txData)
{
    uint8_t i;

    for (i = 0; i < 8u; i++)
    {
        if ((txData & 0x80u) != 0u)
        {
            FLASH_SPI_MOSI_HIGH();
        }
        else
        {
            FLASH_SPI_MOSI_LOW();
        }
        txData <<= 1;
        FLASH_SPI_DELAY();
        FLASH_SPI_SCK_HIGH();
        FLASH_SPI_DELAY();
        FLASH_SPI_SCK_LOW();
    }
}

#endif /* FONT_USE_HW_SPI == 0 */
