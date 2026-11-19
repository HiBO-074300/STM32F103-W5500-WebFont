/**
  ******************************************************************************
  * @文件名		fontlib.h
  * @简介	 	函数声明。
  ******************************************************************************
  * @作者		HiBo
  * @时间		2024-09-25
  * @注意		
  *
  *
  ******************************************************************************
*/
#ifndef __FONTLIB_H
#define __FONTLIB_H

#include "main.h"
#include "stm32f1xx.h"
#include "flash_spi.h"

/* 字库 Flash 使用 F2_CS(PA15)，切换时自动释放 F1_CS(PA12) */
#define FONT_CS_0 FLASH_SPI_SelectF2()
#define FONT_CS_1 FLASH_SPI_Deselect()



// 定义枚举类型表示字体大小  
typedef enum {  
    FONT_SIZE_12x12 = 0,  
    FONT_SIZE_15x16,
    FONT_SIZE_24x24,
    FONT_SIZE_32x32,
	FONT_SIZE_48x48  
} FontSize;  

// 定义枚举类型表示字符大小  
typedef enum {   
    CHAR_SIZE_6x12 = 0,  
    CHAR_SIZE_8x16,   
    CHAR_SIZE_12x24,  
    CHAR_SIZE_16x32, 
    CHAR_SIZE_24x48, 
    CHAR_SIZE_5x7,  
    CHAR_SIZE_7x8 
} CharSize;  
  

CharSize Read_One_Char(uint8_t addr,uint8_t *data,CharSize datasize);
FontSize Read_One_Font(uint16_t addr,uint8_t *data,FontSize datasize);

#endif
