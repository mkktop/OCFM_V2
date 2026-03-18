/**
 * @file app_model.c
 * @brief 应用数据模型层实现
 * @details 实现数据模型的初始化和更新逻辑
 * 
 * @see app_model.h
 */

#include "app_model.h"
#include "global.h"
#include "rtc_time.h"
#include <stdio.h>

/**
 * @brief 全局应用数据模型实例
 * @details 该实例在整个程序运行期间存在，供 UI 层读取数据
 * @note 使用零初始化，确保所有成员初始值为 0 或 NULL
 */
AppDataModel g_app_model = {0};

/**
 * @brief 初始化应用数据模型
 * @details 在系统启动时调用，初始化数据模型的初始状态
 * 
 * @note 必须在 ui_create() 之前调用，因为 ui_create() 会初始化 LVGL Subject
 * 
 * @par 初始化内容
 * - record_time_sec：累计记录时间初始化为 0
 * - total_flow：累计流量初始化为 0.0
 * - time_str 和 record_time_str 初始化为零字符串
 * 
 * @see app_model_update()
 */
void app_model_init(void)
{
    // 初始化累计记录时间为 0 秒
    g_app_model.record_time_sec = 0;
    // 初始化累计流量为 0.0
    g_app_model.total_flow = 0.0;
}

/**
 * @brief 格式化累计时间为字符串
 * @param total_sec 总秒数
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * 
 * @note 这是一个静态（内部）函数，只在本文件中使用
 * 
 * @par 输出格式
 * 格式为：N day HH:MM:SS，例如：125 day 08:30:15
 * - N：天数
 * - HH：小时（00-23）
 * - MM：分钟（00-59）
 * - SS：秒（00-59）
 */
static void format_record_time(uint32_t total_sec, char *buf, size_t buf_size)
{
    // 计算天数：总秒数 / (24小时 * 3600秒)
    uint32_t days = total_sec / (24 * 3600);
    // 计算小时：(总秒数 % 一天秒数) / 3600秒
    uint32_t hours = (total_sec % (24 * 3600)) / 3600;
    // 计算分钟：(总秒数 % 3600秒) / 60秒
    uint32_t minutes = (total_sec % 3600) / 60;
    // 计算秒数：总秒数 % 60秒
    uint32_t seconds = total_sec % 60;

    // 格式化输出字符串
    snprintf(buf, buf_size, "%lu day %02lu:%02lu:%02lu", 
             (unsigned long)days, 
             (unsigned long)hours, 
             (unsigned long)minutes, 
             (unsigned long)seconds);
}

/**
 * @brief 更新应用数据模型
 * @details 从 RTC 获取当前时间，并更新累计记录时间
 * 
 * @note 该函数会被 LVGL 定时器每调用一次，更新以下数据：
 *       - 当前时间（从 RTC 读取，格式：YYYY/MM/DD HH:MM:SS）
 *       - 累计记录时间（递增 1 秒）
 *       - 累计记录时间字符串（格式：N day HH:MM:SS）
 * 
 * @warning 该函数中不能执行耗时操作，否则会影响 UI 响应
 * 
 * @par 数据流向
 * 1. 从 RTC 硬件读取当前时间
 * 2. 格式化当前时间为字符串，存入 time_str
 * 3. 累计记录时间递增 1 秒
 * 4. 格式化累计时间并存入 record_time_str
 * 
 * @see app_model_init()
 */
void app_model_update(void)
{
    // 从 RTC 读取当前时间
    RTC_TimeData time_data;
    RTC_Time_Get(&time_data);

    // 格式化当前时间为字符串
    // 格式：YYYY/MM/DD HH:MM:SS，例如：2026/03/18 14:30:45
    snprintf(g_app_model.time_str, sizeof(g_app_model.time_str),
             "%04d/%02d/%02d %02d:%02d:%02d",
             time_data.year, time_data.month, time_data.date,
             time_data.hour, time_data.minute, time_data.second);

    // 格式化简短时间字符串
    // 格式：HH:MM，例如：14:30
    snprintf(g_app_model.time_short_str, sizeof(g_app_model.time_short_str),
             "%02d:%02d",
             time_data.hour, time_data.minute);

    // 累计记录时间递增 1 秒
    g_app_model.record_time_sec++;

    // 格式化累计时间为字符串
    // 格式：N day HH:MM:SS，例如：125 day 08:30:15
    format_record_time(g_app_model.record_time_sec,
                       g_app_model.record_time_str,
                       sizeof(g_app_model.record_time_str));
}
