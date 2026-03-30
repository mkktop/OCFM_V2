/**
 * @file    ct1820.c
 * @brief   CT1820 (GX1832G) 温度传感器驱动
 * @note    1-Wire协议 bit-bang 实现，适配 STM32F407 @168MHz
 *          非阻塞式：CT1820_StartConvert() 与 CT1820_GetTemp() 分开调用
 */

#include "ct1820.h"

/* 1-Wire 命令 */
#define SKIP_ROM              0xCC
#define CONVERT_TEMP          0x44
#define READ_SCRATCHPAD       0xBE

/* ---- GPIO 方向切换：直接操作MODER寄存器 ---- */

#define DQ_MODER_OUT()   (CT1820_GPIO_Port->MODER = (CT1820_GPIO_Port->MODER & ~3U) | 1U)
#define DQ_MODER_IN()    (CT1820_GPIO_Port->MODER = CT1820_GPIO_Port->MODER & ~3U)

#define DQ_H()    HAL_GPIO_WritePin(CT1820_GPIO_Port, CT1820_Pin, GPIO_PIN_SET)
#define DQ_L()    HAL_GPIO_WritePin(CT1820_GPIO_Port, CT1820_Pin, GPIO_PIN_RESET)
#define DQ_READ() HAL_GPIO_ReadPin(CT1820_GPIO_Port, CT1820_Pin)

/**
 * @brief  微秒级延时
 * @param  us: 延时微秒数
 * @note   168MHz下 ~24次NOP循环/us
 */
static void ct1820_delay_us(uint32_t us)
{
    while (us--) {
        __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP();
    }
}

/**
 * @brief  复位脉冲
 */
static void ct1820_reset(void)
{
    DQ_MODER_OUT();
    DQ_L();
    ct1820_delay_us(600);

    DQ_H();
    DQ_MODER_IN();
    ct1820_delay_us(60);

    /* 忽略应答 */
    ct1820_delay_us(500);
}

/**
 * @brief  写1个字节
 * @param  data: 要写入的字节
 */
static void ct1820_write_byte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++) {
        DQ_MODER_OUT();
        DQ_L();
        ct1820_delay_us(5);

        if (data & 0x01)
            DQ_H();
        else
            DQ_L();

        ct1820_delay_us(80);
        DQ_H();

        data >>= 1;
    }
}

/**
 * @brief  读1个字节 (CT1820兼容时序)
 * @retval 读取到的字节
 */
static uint8_t ct1820_read_byte(void)
{
    uint8_t byte = 0;

    for (uint8_t i = 0; i < 8; i++) {
        DQ_MODER_OUT();
        DQ_L();
        DQ_MODER_IN();
        byte = (byte >> 1) | (DQ_READ() ? 0x80 : 0x00);
        ct1820_delay_us(48);
    }

    return byte;
}

/**
 * @brief  初始化CT1820
 */
void CT1820_Init(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = CT1820_Pin;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(CT1820_GPIO_Port, &g);
    DQ_H();

    ct1820_reset();
}

/**
 * @brief  发起温度转换 (非阻塞)
 * @note   调用后需等待 >= 750ms 再调用 CT1820_GetTemp()
 */
void CT1820_StartConvert(void)
{
    ct1820_reset();
    ct1820_write_byte(SKIP_ROM);
    ct1820_write_byte(CONVERT_TEMP);
}

/**
 * @brief  读取温度值
 * @retval 温度值 x10 (例如 256 表示 25.6°C)
 */
int16_t CT1820_GetTemp(void)
{
    uint8_t temp_h, temp_l;
    uint16_t raw;
    int32_t temp;

    ct1820_reset();
    ct1820_write_byte(SKIP_ROM);
    ct1820_write_byte(READ_SCRATCHPAD);

    temp_l = ct1820_read_byte();
    temp_h = ct1820_read_byte();

    /* 过滤全1(断线) */
    if (temp_h == 0xFF && temp_l == 0xFF)
        return 0;

    raw = ((uint16_t)temp_h << 8) | temp_l;

    /* 12bit: raw * 0.0625 °C，放大10倍 */
    temp = (int32_t)(int16_t)raw * 625 / 1000;

    return (int16_t)temp;
}
