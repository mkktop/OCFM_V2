#ifndef __APP_FLOW_CALC_H__
#define __APP_FLOW_CALC_H__

#include "app_sensor.h"
#include "app_config.h"
#include "global.h"
#include <math.h>

/*============================================================================*/
/*                           对外接口                                           */
/*============================================================================*/

/**
 * @brief  更新流量计算 (每秒调用)
 * @note   从传感器获取水位，计算瞬时流量并累加累计流量
 */
void flow_calc_update(void);

/**
 * @brief  获取当前瞬时流量
 * @retval 瞬时流量 (根据配置的单位)
 */
float flow_calc_get_instant(void);

/**
 * @brief  获取累计流量
 * @retval 累计流量 (m³)
 */
double flow_calc_get_total(void);

/**
 * @brief  清零累计流量
 */
void flow_calc_reset_total(void);

/**
 * @brief  从备份寄存器加载累计流量
 */
void flow_calc_load_total(void);

/**
 * @brief  保存累计流量到备份寄存器
 */
void flow_calc_save_total(void);

/**
 * @brief  处理EEPROM保存请求 (在主循环中调用)
 * @note   避免在定时器中直接操作I2C，防止阻塞
 */
void flow_calc_process(void);

#endif /* __APP_FLOW_CALC_H__ */
