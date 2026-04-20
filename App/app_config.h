/**
 * @file app_config.h
 * @brief 系统配置管理头文件
 * @note 管理SystemConfig_t的EEPROM存储和默认值初始化
 *       统一配置参数访问入口，UI/Modbus/4G/LoRa等模块均通过此API读写配置
 */

#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#include "global.h"
#include <stdint.h>

/**
 * @brief 配置参数ID (协议无关)
 * @note 所有模块通过此枚举访问配置参数
 */
typedef enum {
    /* 基本参数 */
    CONFIG_ID_RANGE_MAX       = 0,
    CONFIG_ID_HEIGHT,
    CONFIG_ID_CALIBRATION_4MA,
    CONFIG_ID_CALIBRATION_20MA,
    CONFIG_ID_RANGE_4MA,       /* float */
    CONFIG_ID_RANGE_20MA,      /* float */
    CONFIG_ID_POINT_NUM,
    /* 测量参数 */
    CONFIG_ID_WINDOW_WIDTH,
    CONFIG_ID_FILTER_COUNT,
    CONFIG_ID_DELAY_TIME,
    CONFIG_ID_ANTENNA_TYPE,
    CONFIG_ID_BLIND_AREA,
    CONFIG_ID_W_COEFF,
    CONFIG_ID_M_COEFF,
    /* Modbus参数 */
    CONFIG_ID_MODBUS_ADDR,
    CONFIG_ID_MODBUS_BAUDRATE,
    CONFIG_ID_MODBUS_STOPBITS,
    /* 报警参数 (float) */
    CONFIG_ID_ALARM_AH,
    CONFIG_ID_ALARM_AL,
    CONFIG_ID_ALARM_DH,
    CONFIG_ID_ALARM_DL,
    CONFIG_ID_ALARM_AAH,
    CONFIG_ID_ALARM_AAL,
    /* 其他参数 */
    CONFIG_ID_DIS_OFFSET,
    CONFIG_ID_CANALS_TYPE,
    CONFIG_ID_CHANNEL_ID,
    CONFIG_ID_INSTANT_UNIT,
    CONFIG_ID_SUM_POINT,
    CONFIG_ID_LANGUAGE,
    CONFIG_ID_SHOW_ALARM,
    CONFIG_ID_PASSWORD_ENABLE,
    /* 特殊动作 */
    CONFIG_ID_FACTORY_RESET,
    CONFIG_ID_CLEAR_TOTAL,

    CONFIG_ID_COUNT   /* 参数总数 */
} config_id_t;

/**
 * @brief 统一API返回码
 */
#define CONFIG_OK           0   /* 成功 */
#define CONFIG_ERR_ID       1   /* 无效的config_id */
#define CONFIG_ERR_READONLY 2   /* 只读参数 */
#define CONFIG_ERR_RANGE    3   /* 值超出范围 */

/*============================================================================*/
/*                           系统配置管理                                       */
/*============================================================================*/

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
/*                           统一配置参数API                                    */
/*============================================================================*/

/**
 * @brief 设置uint32配置参数
 * @param id:    配置参数ID
 * @param value: 新值
 * @retval CONFIG_OK 成功, 其他见错误码定义
 * @note 设置成功后标记dirty，由app_config_process()延迟保存到EEPROM
 */
uint8_t app_config_set(config_id_t id, uint32_t value);

/**
 * @brief 设置float配置参数
 * @param id:    配置参数ID (必须是float类型参数)
 * @param value: 新值
 * @retval CONFIG_OK 成功, 其他见错误码定义
 */
uint8_t app_config_setf(config_id_t id, float value);

/**
 * @brief 获取uint32配置参数
 * @param id:    配置参数ID
 * @param value: 输出值指针
 * @retval CONFIG_OK 成功, CONFIG_ERR_ID 无效ID
 */
uint8_t app_config_get_val(config_id_t id, uint32_t *value);

/**
 * @brief 获取float配置参数
 * @param id:    配置参数ID
 * @param value: 输出值指针
 * @retval CONFIG_OK 成功, CONFIG_ERR_ID 无效ID
 */
uint8_t app_config_getf(config_id_t id, float *value);

/**
 * @brief 判断配置参数是否为float类型
 * @param id: 配置参数ID
 * @retval 1: float类型  0: uint32类型
 */
uint8_t app_config_is_float(config_id_t id);

/**
 * @brief 处理延迟保存请求
 * @note 需要在主循环中周期性调用
 *       当有脏数据且超过延迟时间后，执行EEPROM写入
 */
void app_config_process(void);

/**
 * @brief 获取EEPROM互锁
 * @retval 1: 成功获取, 0: EEPROM忙
 * @note  其他模块写EEPROM前调用，防止与config模块冲突
 */
uint8_t app_config_eeprom_lock(void);

/**
 * @brief 释放EEPROM互锁
 */
void app_config_eeprom_unlock(void);

/**
 * @brief 参数变更回调类型
 */
typedef void (*config_change_callback_t)(config_id_t id);

/**
 * @brief 注册参数变更回调
 * @param cb: 回调函数指针，参数变更时被调用
 * @retval 0: 成功, 1: 已满
 * @note 最多支持4个监听者，重复注册同一回调视为成功
 */
uint8_t app_config_set_change_callback(config_change_callback_t cb);

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

uint32_t app_config_get_show_alarm(void);
void app_config_set_show_alarm(uint32_t value);

uint32_t app_config_get_password_enable(void);
void app_config_set_password_enable(uint32_t value);

#endif /* __APP_CONFIG_H */
