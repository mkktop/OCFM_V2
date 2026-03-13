/**
 * @file log_manager.h
 * @brief 日志管理器头文件
 * @details 提供系统日志、操作日志、流量数据的记录功能
 *          支持按日期自动分文件、日志级别过滤、循环覆盖
 */

#ifndef __LOG_MANAGER_H
#define __LOG_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "file_driver.h"
#include <stdarg.h>

/**
 * @brief 日志级别定义
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,    /**< 调试信息 */
    LOG_LEVEL_INFO  = 1,    /**< 一般信息 */
    LOG_LEVEL_WARN  = 2,    /**< 警告信息 */
    LOG_LEVEL_ERROR = 3     /**< 错误信息 */
} LogLevel;

/**
 * @brief 日志类型定义
 */
typedef enum {
    LOG_TYPE_SYSTEM = 0,    /**< 系统日志 */
    LOG_TYPE_OPER   = 1,    /**< 操作日志 */
    LOG_TYPE_FLOW   = 2,    /**< 流量数据 */
    LOG_TYPE_ALARM  = 3     /**< 报警日志 */
} LogType;

/**
 * @brief 日志管理器配置结构体
 */
typedef struct {
    uint8_t enable_console;         /**< 是否输出到串口控制台 */
    uint8_t enable_sd;              /**< 是否保存到SD卡 */
    LogLevel min_level;             /**< 最低记录级别 */
    uint32_t max_file_size;         /**< 单个文件最大大小(字节) */
    uint32_t max_file_count;        /**< 每种类型最大文件数(循环覆盖) */
    char base_path[32];             /**< 日志根目录 */
} LogManagerConfig;

/**
 * @brief 日志管理器初始化
 * @param config 配置指针，传NULL使用默认配置
 * @return 0:成功 1:失败
 * @note 默认配置: INFO级别, 最大1MB/文件, 10个文件循环
 */
uint8_t log_manager_init(const LogManagerConfig* config);

/**
 * @brief 设置最低日志级别
 * @param level 最低级别，低于此级别的日志将被忽略
 */
void log_set_level(LogLevel level);

/**
 * @brief 写入格式化日志
 * @param level 日志级别
 * @param fmt 格式字符串，支持printf格式
 * @param ... 可变参数
 * @return 0:成功 1:失败
 */
uint8_t log_write(LogLevel level, const char* fmt, ...);

/**
 * @brief 写入系统日志
 * @param event 事件名称
 * @param detail 详细描述
 * @return 0:成功 1:失败
 */
uint8_t log_system(const char* event, const char* detail);

/**
 * @brief 写入操作日志
 * @param operator_name 操作者名称
 * @param action 操作动作
 * @param result 操作结果
 * @return 0:成功 1:失败
 */
uint8_t log_operation(const char* operator_name, const char* action, const char* result);

/**
 * @brief 写入流量数据日志
 * @param timestamp 时间戳字符串(格式: YYYY-MM-DD HH:MM:SS)
 * @param water_level 水位值(m)
 * @param instant_flow 瞬时流量(m³/s)
 * @param total_flow 累计流量(m³)
 * @return 0:成功 1:失败
 */
uint8_t log_flow_data(const char* timestamp, float water_level,
                      float instant_flow, double total_flow);

/**
 * @brief 写入报警日志
 * @param alarm_type 报警类型
 * @param alarm_level 报警级别(1-4)
 * @param description 报警描述
 * @return 0:成功 1:失败
 */
uint8_t log_alarm(const char* alarm_type, uint8_t alarm_level, const char* description);

/**
 * @brief 获取当前日志目录
 * @param log_type 日志类型
 * @param path 输出路径缓冲区
 * @param path_size 缓冲区大小
 * @return 0:成功 1:失败
 */
uint8_t log_get_current_path(LogType log_type, char* path, uint32_t path_size);

/**
 * @brief 列出指定类型的日志文件
 * @param log_type 日志类型
 * @return 0:成功 1:失败
 */
uint8_t log_list_files(LogType log_type);

/**
 * @brief 删除指定日期的日志
 * @param log_type 日志类型
 * @param date 日期字符串(格式: YYYYMMDD)
 * @return 0:成功 1:失败
 */
uint8_t log_delete_by_date(LogType log_type, const char* date);

/**
 * @brief 清理过期日志文件
 * @param keep_days 保留天数
 * @return 0:成功 1:失败
 */
uint8_t log_cleanup(uint32_t keep_days);

/**
 * @brief 强制刷新日志到SD卡
 * @return 0:成功 1:失败
 */
uint8_t log_flush(void);

/**
 * @brief 关闭日志管理器
 */
void log_manager_deinit(void);

/**
 * @brief 获取错误计数
 * @return 错误次数
 */
uint32_t log_get_error_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __LOG_MANAGER_H */
