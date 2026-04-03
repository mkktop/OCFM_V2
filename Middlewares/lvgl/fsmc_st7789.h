#ifndef __FSMC_ST7789_H__
#define __FSMC_ST7789_H__
#include "main.h"
#include "fsmc.h"

#define LCD_CMD_ADDR  ((uint32_t)0x60000000)  /* A22=0, RS=0 */
#define LCD_DATA_ADDR ((uint32_t)0x60400000)  /* A22=1, RS=1 */

/* ��ʼ������ */
void fsmc_st7789_init(void);

/* ������ͼ���� */
void fsmc_st7789_fill_rect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void fsmc_st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void fsmc_st7789_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/* ���Ժ��� */
void fsmc_st7789_test_pattern(void);


/* 背光控制 */
void fsmc_st7789_backlight_on(void);
void fsmc_st7789_backlight_off(void);
#endif /* __FSMC_ST7789_H__ */
