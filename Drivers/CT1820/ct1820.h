/**
 * @file    ct1820.h
 * @brief   CT1820 (GX1832G) 温度传感器驱动
 * @note    基于1-Wire协议，使用PB0引脚。非阻塞式设计。
 */

#ifndef __CT1820_H
#define __CT1820_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @brief  初始化CT1820
 */
void CT1820_Init(void);

/**
 * @brief  发起温度转换 (非阻塞)
 * @note   调用后需等待 >= 750ms 再调用 CT1820_GetTemp()
 */
void CT1820_StartConvert(void);

/**
 * @brief  读取温度值 (非阻塞，微秒级忙等约2ms)
 * @retval 温度值 x10 (例如 256 表示 25.6°C)
 * @note   需在 CT1820_StartConvert() 之后 >= 750ms 调用
 */
int16_t CT1820_GetTemp(void);

#ifdef __cplusplus
}
#endif

#endif /* __CT1820_H */
