#include "fsmc_st7789.h"
#include "main.h"
#include <stdint.h>

/* 私有函数声明 */
static void lcd_write_cmd(uint8_t cmd);
static void lcd_write_data(uint8_t data);
static void lcd_write_data_16bit(uint16_t data);
static void lcd_reset(void);

/* 写命令到LCD */
static void lcd_write_cmd(uint8_t cmd)
{
    /* 访问命令地址（RS=0） */
    *(volatile uint8_t *)LCD_CMD_ADDR = cmd;
}

/* 写8位数据到LCD */
static void lcd_write_data(uint8_t data)
{
    /* 访问数据地址（RS=1） */
    *(volatile uint8_t *)LCD_DATA_ADDR = data;
}

/* 写16位数据到LCD（用于像素数据） */
static void lcd_write_data_16bit(uint16_t data)
{
    /* 由于是8位接口，需要分两次写入 */
    *(volatile uint8_t *)LCD_DATA_ADDR = data >> 8;    /* 高8位 */
    *(volatile uint8_t *)LCD_DATA_ADDR = data & 0xFF;  /* 低8位 */
}

/* 硬件复位LCD */
static void lcd_reset(void)
{
    HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
    HAL_Delay(100);
}

/* 设置显示区域 */
void fsmc_st7789_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    /* 设置列地址 */
    lcd_write_cmd(0x2A);  
    lcd_write_data(x1 >> 8);
    lcd_write_data(x1 & 0xFF);
    lcd_write_data(x2 >> 8);
    lcd_write_data(x2 & 0xFF);
    
    /* 设置行地址 */
    lcd_write_cmd(0x2B);  
    lcd_write_data(y1 >> 8);
    lcd_write_data(y1 & 0xFF);
    lcd_write_data(y2 >> 8);
    lcd_write_data(y2 & 0xFF);
    
    /* 开始写入GRAM数据 */
    lcd_write_cmd(0x2C); 
}

/**
 * @brief ST7789 LCD初始化函数
 * @details 初始化ST7789显示控制器，包括硬件复位、电源配置、伽马校正等
 * @note 
 */
void fsmc_st7789_init(void)
{
    /* 1. 硬件复位 */
    /* 通过拉低RESET引脚并延迟，使LCD进入复位状态 */
    lcd_reset();
    
    /* 2. 开启背光 */
    /* LCD_BLK引脚控制背光 */
    HAL_GPIO_WritePin(LCD_BLK_GPIO_Port, LCD_BLK_Pin, GPIO_PIN_SET);
    
    /* 3. ST7789初始化命令序列 */
    /* 0x01: 软件复位命令 */
    /* 执行软件复位，使LCD内部寄存器恢复到默认状态 */
    lcd_write_cmd(0x01);
    HAL_Delay(150);  // 等待150ms让复位完成
    
    /* 0x11: 退出睡眠模式 */
    /* 退出睡眠模式，LCD开始正常工作 */
    lcd_write_cmd(0x11);
    HAL_Delay(120);  // 等待120ms让LCD稳定
    
    /* 0x3A: 设置接口像素格式 */
    /* 参数0x55表示使用16位/像素，RGB565格式（5位红+6位绿+5位蓝） */
    lcd_write_cmd(0x3A);
    lcd_write_data(0x55);
    
    /* 0x36: 内存数据访问控制 */
    /* 设置显示方向和扫描顺序，0x00为正常方向 */
    lcd_write_cmd(0x36);
    lcd_write_data(0x60);
    
    /* 0xB2:  porch设置 */
    /* 配置显示区域的 porch参数，用于改善显示效果 */
    lcd_write_cmd(0xB2);
    lcd_write_data(0x0C);
    lcd_write_data(0x0C);
    lcd_write_data(0x00);
    lcd_write_data(0x33);
    lcd_write_data(0x33);
    
    /* 0xB7: 门控控制 */
    /* 设置门控电压参数 */
    lcd_write_cmd(0xB7);
    lcd_write_data(0x35);
    
    /* 0xBB: VCOM设置 */
    /* 配置Vcom电压，影响显示对比度 */
    lcd_write_cmd(0xBB);
    lcd_write_data(0x2B);
    
    /* 0xC0: LCM控制 */
    /* 设置LCM（液晶模块）工作参数 */
    lcd_write_cmd(0xC0);
    lcd_write_data(0x2C);
    
    /* 0xC2: VDV和VRH命令使能 */
    /* 使能VDV和VRH电压设置命令 */
    lcd_write_cmd(0xC2);
    lcd_write_data(0x01);
    
    /* 0xC3: VRH设置 */
    /* 设置VRH输出电压 */
    lcd_write_cmd(0xC3);
    lcd_write_data(0x0B);
    
    /* 0xC4: VDV设置 */
    /* 设置VDV电压 */
    lcd_write_cmd(0xC4);
    lcd_write_data(0x20);
    
    /* 0xC6: 帧率控制 */
    /* 配置正常模式下的帧率，0x0F约为60Hz */
    lcd_write_cmd(0xC6);
    lcd_write_data(0x0F);
    
    /* 0xD0: 电源控制1 */
    /* 设置电源控制参数 */
    lcd_write_cmd(0xD0);
    lcd_write_data(0xA4);
    lcd_write_data(0xA1);
    
    /* 0xE0: 正向电压伽马校正 */
    /* 配置伽马曲线，优化灰度显示效果 */
    lcd_write_cmd(0xE0);
    lcd_write_data(0xD0);
    lcd_write_data(0x00);
    lcd_write_data(0x02);
    lcd_write_data(0x07);
    lcd_write_data(0x0A);
    lcd_write_data(0x28);
    lcd_write_data(0x32);
    lcd_write_data(0x44);
    lcd_write_data(0x42);
    lcd_write_data(0x06);
    lcd_write_data(0x0E);
    lcd_write_data(0x12);
    lcd_write_data(0x14);
    lcd_write_data(0x17);
    
    /* 0xE1: 负向电压伽马校正 */
    /* 配置反向伽马曲线 */
    lcd_write_cmd(0xE1);
    lcd_write_data(0xD0);
    lcd_write_data(0x00);
    lcd_write_data(0x02);
    lcd_write_data(0x07);
    lcd_write_data(0x0A);
    lcd_write_data(0x28);
    lcd_write_data(0x31);
    lcd_write_data(0x54);
    lcd_write_data(0x47);
    lcd_write_data(0x0E);
    lcd_write_data(0x1C);
    lcd_write_data(0x17);
    lcd_write_data(0x1B);
    lcd_write_data(0x1E);
    
    /* 0x20: 显示反显关闭 */
    /* 关闭显示反显功能，正常显示颜色 */
    lcd_write_cmd(0x20);
    
    /* 0x29: 显示开启 */
    /* 开启显示，LCD开始显示内容 */
    lcd_write_cmd(0x29);
    HAL_Delay(120);  // 等待120ms让显示稳定
}

/* 填充矩形区域 */
void fsmc_st7789_fill_rect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint32_t pixel_count = (x2 - x1 + 1) * (y2 - y1 + 1);
    
    /* 设置显示区域 */
    fsmc_st7789_set_window(x1, y1, x2, y2);
    
    /* 性能优化：减少函数调用和内存访问 */
    volatile uint8_t *data_addr = (volatile uint8_t *)LCD_DATA_ADDR;
    uint8_t high_byte = color >> 8;
    uint8_t low_byte = color & 0xFF;
    
    for(uint32_t i = 0; i < pixel_count; i++) {
        *data_addr = high_byte;
        *data_addr = low_byte;
    }
}

