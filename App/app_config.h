/**
 * @file app_config.h
 * @brief 系统配置管理头文件
 * @note 管理SystemConfig_t的EEPROM存储和默认值初始化
 */

#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#include "global.h"
#include <stdint.h>

/**
 * @brief 初始化系统配置
 * @retval None
 * @note 从EEPROM读取配置，若无效则使用默认值初始化
 */
void app_config_init(void);

/**
 * @brief 保存系统配置到EEPROM
 * @retval 1: 成功 0: 失败
 */
uint8_t app_config_save(void);

/**
 * @brief 加载系统配置从EEPROM
 * @retval 1: 成功 0: 失败
 */
uint8_t app_config_load(void);

/**
 * @brief 使用默认值初始化配置
 * @retval None
 */
void app_config_set_default(void);

/**
 * @brief 获取系统配置指针
 * @retval SystemConfig_t结构体指针
 */
SystemConfig_t* app_config_get(void);

/**
 * @brief 检查配置是否有效
 * @retval 1: 有效 0: 无效
 */
uint8_t app_config_is_valid(void);

/**
 * @brief 恢复出厂设置
 * @retval 1: 成功 0: 失败
 */
uint8_t app_config_factory_reset(void);

/*============================================================================*/
/*                           基本参数 Getter/Setter                             */
/*============================================================================*/
uint32_t app_config_get_range_max(void);
void app_config_set_range_max(uint32_t value);

uint32_t app_config_get_height(void);
void app_config_set_height(uint32_t value);

uint32_t app_config_get_calibration_4ma(void);
void app_config_set_calibration_4ma(uint32_t value);

uint32_t app_config_get_calibration_20ma(void);
void app_config_set_calibration_20ma(uint32_t value);

float app_config_get_range_4ma(void);
void app_config_set_range_4ma(float value);

float app_config_get_range_20ma(void);
void app_config_set_range_20ma(float value);

uint32_t app_config_get_point_num(void);
void app_config_set_point_num(uint32_t value);

/*============================================================================*/
/*                           测量参数 Getter/Setter                             */
/*============================================================================*/
uint32_t app_config_get_window_width(void);
void app_config_set_window_width(uint32_t value);

uint32_t app_config_get_filter_count(void);
void app_config_set_filter_count(uint32_t value);

uint32_t app_config_get_delay_time(void);
void app_config_set_delay_time(uint32_t value);

uint32_t app_config_get_antenna_type(void);
void app_config_set_antenna_type(uint32_t value);

uint32_t app_config_get_blind_area(void);
void app_config_set_blind_area(uint32_t value);

uint32_t app_config_get_w_coeff(void);
void app_config_set_w_coeff(uint32_t value);

uint32_t app_config_get_m_coeff(void);
void app_config_set_m_coeff(uint32_t value);

/*============================================================================*/
/*                           Modbus参数 Getter/Setter                           */
/*============================================================================*/
uint32_t app_config_get_modbus_addr(void);
void app_config_set_modbus_addr(uint32_t value);

uint32_t app_config_get_modbus_baudrate(void);
void app_config_set_modbus_baudrate(uint32_t value);

uint32_t app_config_get_modbus_stopbits(void);
void app_config_set_modbus_stopbits(uint32_t value);

/*============================================================================*/
/*                           报警参数 Getter/Setter                             */
/*============================================================================*/
float app_config_get_alarm_ah(void);
void app_config_set_alarm_ah(float value);

float app_config_get_alarm_al(void);
void app_config_set_alarm_al(float value);

float app_config_get_alarm_dh(void);
void app_config_set_alarm_dh(float value);

float app_config_get_alarm_dl(void);
void app_config_set_alarm_dl(float value);

float app_config_get_alarm_aah(void);
void app_config_set_alarm_aah(float value);

float app_config_get_alarm_aal(void);
void app_config_set_alarm_aal(float value);

/*============================================================================*/
/*                           其他参数 Getter/Setter                             */
/*============================================================================*/
uint32_t app_config_get_factory_settings(void);
void app_config_set_factory_settings(uint32_t value);

uint32_t app_config_get_dis_offset(void);
void app_config_set_dis_offset(uint32_t value);

uint32_t app_config_get_canals_type(void);
void app_config_set_canals_type(uint32_t value);

uint32_t app_config_get_channel_id(void);
void app_config_set_channel_id(uint32_t value);

uint32_t app_config_get_instant_unit(void);
void app_config_set_instant_unit(uint32_t value);

uint32_t app_config_get_sum_point(void);
void app_config_set_sum_point(uint32_t value);

uint32_t app_config_get_language(void);
void app_config_set_language(uint32_t value);

#endif /* __APP_CONFIG_H */
