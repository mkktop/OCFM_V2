/**
 * @file app_model.h
 * @brief 应用数据模型层
 * @details 统一管理应用所有显示数据，为 UI 层提供数据来源
 */

#ifndef __APP_MODEL_H__
#define __APP_MODEL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "global.h"

/**
 * @brief 应用数据模型结构体
 * @details 存储所有需要在 UI 上显示的数据
 * 
 * @note 新增参数时，请确保在 ui_conf.h 中添加对应的 Subject
 */
typedef struct {
    char time_str[32];          ///< 当前时间字符串，格式：YYYY/MM/DD HH:MM:SS
    char time_short_str[16];    ///< 简短时间字符串，格式：HH:MM
    uint32_t total_time_sec;    ///< 累计时长 (秒)
    char total_time_str[32];    ///< 累计时长字符串，格式：N day HH:MM:SS
    char water_level_str[16];   ///< 水位字符串，格式：L:x.xxxm
    char instant_flow_str[16];  ///< 瞬时流量字符串
    char current_ma_str[16];    ///< 4-20mA输出电流字符串，格式：xx.xxmA
    char total_flow_str[24];    ///< 累计流量字符串
    double total_flow;          ///< 累计流量 (m³)
    float instant_flow;         ///< 瞬时流量 (L/s)
    float water_level_m;        ///< 当前水位
    uint8_t sensor_online;      ///< 传感器在线状态 (1:在线 0:离线)
} AppDataModel;

/**
 * @brief 全局应用数据模型实例
 * @details 该实例在整个程序运行期间存在，供 UI 层读取数据
 * @note 为 extern 变量，在 app_model.c 中定义
 */
extern AppDataModel g_app_model;

/**
 * @brief 初始化应用数据模型
 * @details 在系统启动时调用，初始化数据模型的初始状态
 * 
 * @note 必须在 ui_create() 之前调用，因为 ui_create() 会初始化 LVGL Subject
 * 
 * @see app_model_update()
 */
void app_model_init(void);

/**
 * @brief 更新应用数据模型
 * @details 从 RTC/传感器获取最新数据并更新到数据模型中
 * 
 * @note 该函数会被 LVGL 定时器每调用一次，更新以下数据：
 *       - 当前时间（从 RTC 读取）
 *       - 流量数据（从 app_flow_calc 获取）
 *       - 传感器状态（从 app_sensor 获取）
 * 
 * @warning 该函数中不能执行耗时操作，否则会影响 UI 响应
 * 
 * @see app_model_init()
 */
void app_model_update(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MODEL_H__ */
