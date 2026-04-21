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
 * @param  water_level_m: 水位 (米), <=0表示传感器离线
 * @note   计算瞬时流量并累加累计流量
 */
void flow_calc_update(float water_level_m);

/**
 * @brief  获取当前瞬时流量
 * @retval 瞬时流量 (根据配置的单位)
 */
float flow_calc_get_instant(void);

/**
 * @brief  获取当前瞬时流量 (原始L/s)
 * @retval 瞬时流量 (L/s，未经单位转换)
 */
float flow_calc_get_instant_lps(void);

/**
 * @brief  获取累计流量
 * @retval 累计流量 (m³)
 */
double flow_calc_get_total(void);

/**
 * @brief  清零累计流量和累计时长
 */
void flow_calc_reset_total(void);

/**
 * @brief  设置累计流量值
 * @param  value: 累计流量 (m³), 须 >= 0
 */
void flow_calc_set_total(double value);

/**
 * @brief  获取累计时长
 * @retval 累计时长 (秒)
 */
uint32_t flow_calc_get_total_time(void);

/**
 * @brief  从备份寄存器/EEPROM加载累计流量和累计时长
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
