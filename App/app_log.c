#include "app_log.h"

/**
 * @brief 初始化数据记录器
 * 
 * 该函数初始化数据记录器与日志管理模块
 * 
 */
void app_log_data_init(void)
{
    // 初始化数据记录器
    data_recorder_init(NULL);
    // 初始化日志管理模块
    log_manager_init(NULL);
    // 写入系统初始化日志
    log_write(LOG_TYPE_SYSTEM, "SYSTEM_INIT");
    // 写入用户登录日志
    log_write(LOG_TYPE_USER, "admin is online");
    // 写入报警系统初始化日志
    log_write(LOG_TYPE_ALARM, "ALARM_SYSTEM_INIT");

}