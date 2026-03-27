/**
 * @file app_model.c
 * @brief 应用数据模型实现
 * @details 实现数据模型的初始化和更新逻辑
 */

#include "app_model.h"
#include "app_flow_calc.h"
#include "app_sensor.h"
#include "global.h"
#include "rtc_time.h"
#include <stdio.h>

/**
 * @brief 全局应用数据模型实例
 */
AppDataModel g_app_model = {0};

/**
 * @brief 初始化应用数据模型
 */
void app_model_init(void)
{
    g_app_model.total_time_sec = 0;
    g_app_model.total_flow = 0.0;
    g_app_model.instant_flow = 0.0f;
    g_app_model.water_level_m = 0.0f;
    g_app_model.sensor_online = 0;
}

/**
 * @brief 格式化累计时长为字符串
 */
static void format_total_time(uint32_t total_sec, char *buf, size_t buf_size)
{
    uint32_t days = total_sec / (24 * 3600);
    uint32_t hours = (total_sec % (24 * 3600)) / 3600;
    uint32_t minutes = (total_sec % 3600) / 60;
    uint32_t seconds = total_sec % 60;

    snprintf(buf, buf_size, "%lu day %02lu:%02lu:%02lu",
             (unsigned long)days,
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)seconds);
}

/**
 * @brief 更新应用数据模型
 * @details 每秒调用一次，更新：
 *       - 当前时间
 *       - 累计时长
 *       - 流量数据
 *       - 传感器状态
 */
void app_model_update(void)
{
    SensorData_t *sensor;

    /* 从 RTC 获取当前时间 */
    RTC_TimeData time_data;
    RTC_Time_Get(&time_data);

    snprintf(g_app_model.time_str, sizeof(g_app_model.time_str),
             "%04d/%02d/%02d %02d:%02d:%02d",
             time_data.year, time_data.month, time_data.date,
             time_data.hour, time_data.minute, time_data.second);

    snprintf(g_app_model.time_short_str, sizeof(g_app_model.time_short_str),
             "%02d:%02d",
             time_data.hour, time_data.minute);

    /* 同步累计时长 */
    g_app_model.total_time_sec = flow_calc_get_total_time();
    format_total_time(g_app_model.total_time_sec,
                      g_app_model.total_time_str,
                      sizeof(g_app_model.total_time_str));

    /* 流量计算在 freertos.c 的 flow_refresh_fun 定时器中更新 */

    /* 同步流量数据 */
    g_app_model.instant_flow = flow_calc_get_instant();
    g_app_model.total_flow = flow_calc_get_total();

    /* 格式化流量字符串 */
    snprintf(g_app_model.instant_flow_str, sizeof(g_app_model.instant_flow_str),
             "%.2f", g_app_model.instant_flow);
    snprintf(g_app_model.total_flow_str, sizeof(g_app_model.total_flow_str),
             "%.2f", g_app_model.total_flow);

    /* 同步传感器状态 */
    sensor = app_sensor_get_data();
    if (sensor != NULL) {
        g_app_model.water_level_m = sensor->water_level_m;
        g_app_model.sensor_online = sensor->is_online;
    }

    /* 格式化水位字符串 */
    snprintf(g_app_model.water_level_str, sizeof(g_app_model.water_level_str),
             "L:%.3fm", g_app_model.water_level_m);
}
