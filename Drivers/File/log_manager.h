/**
 * @file log_manager.h
 * @brief 日志管理器头文件
 * @details 提供系统日志、用户操作日志、报警日志的记录和查询功能
 *          支持按日期分目录存储、按时间查询、日志清理
 * @note 存储方式: /LOGS/{SYS|USER|ALARM}/YYYY/MM/DD.log (每天一个文件)
 */

#ifndef __LOG_MANAGER_H
#define __LOG_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "file_driver.h"

/**
 * @brief 日志管理器版本号
 * @note 格式: 0xMMmm (M=主版本, m=次版本)
 */
#define LOG_MANAGER_VERSION     0x0100

/**
 * @brief 单条日志内容最大长度(字节)
 * @note 包含字符串结尾符，实际可用128-1=127字节
 */
#define LOG_CONTENT_MAX_LEN     128

/**
 * @brief 日志存储根目录
 * @note 所有日志存储在SD卡根目录的LOGS文件夹下
 */
#define LOG_BASE_PATH           "/LOGS"

/**
 * @brief 系统日志目录
 * @note 存储系统运行相关日志
 */
#define LOG_SYS_PATH            "/LOGS/SYS"

/**
 * @brief 用户操作日志目录
 * @note 存储用户操作记录日志
 */
#define LOG_USER_PATH           "/LOGS/USER"

/**
 * @brief 报警日志目录
 * @note 存储报警和异常事件日志
 */
#define LOG_ALARM_PATH          "/LOGS/ALARM"

/**
 * @brief 系统日志年目录路径模板
 * @note 格式: /LOGS/SYS/YYYY
 */
#define LOG_SYS_YEAR_DIR        "/LOGS/SYS/%04u"

/**
 * @brief 系统日志月目录路径模板
 * @note 格式: /LOGS/SYS/YYYY/MM
 */
#define LOG_SYS_MONTH_DIR       "/LOGS/SYS/%04u/%02u"

/**
 * @brief 系统日志日文件路径模板
 * @note 格式: /LOGS/SYS/YYYY/MM/DD.log
 */
#define LOG_SYS_DAY_FILE        "/LOGS/SYS/%04u/%02u/%02u.log"

/**
 * @brief 用户日志年目录路径模板
 * @note 格式: /LOGS/USER/YYYY
 */
#define LOG_USER_YEAR_DIR       "/LOGS/USER/%04u"

/**
 * @brief 用户日志月目录路径模板
 * @note 格式: /LOGS/USER/YYYY/MM
 */
#define LOG_USER_MONTH_DIR      "/LOGS/USER/%04u/%02u"

/**
 * @brief 用户日志日文件路径模板
 * @note 格式: /LOGS/USER/YYYY/MM/DD.log
 */
#define LOG_USER_DAY_FILE       "/LOGS/USER/%04u/%02u/%02u.log"

/**
 * @brief 报警日志年目录路径模板
 * @note 格式: /LOGS/ALARM/YYYY
 */
#define LOG_ALARM_YEAR_DIR      "/LOGS/ALARM/%04u"

/**
 * @brief 报警日志月目录路径模板
 * @note 格式: /LOGS/ALARM/YYYY/MM
 */
#define LOG_ALARM_MONTH_DIR     "/LOGS/ALARM/%04u/%02u"

/**
 * @brief 报警日志日文件路径模板
 * @note 格式: /LOGS/ALARM/YYYY/MM/DD.log
 */
#define LOG_ALARM_DAY_FILE      "/LOGS/ALARM/%04u/%02u/%02u.log"

/**
 * @brief 日志最大保留天数
 * @note 清理过期日志时，最大支持保留365天
 */
#define LOG_MAX_RETENTION_DAYS  365

/**
 * @brief 日志类型枚举
 * @details 用于区分不同类型的日志，便于分类查询和管理
 */
typedef enum {
    LOG_TYPE_SYSTEM = 0,    /**< 系统日志: 记录系统运行状态、错误信息等 */
    LOG_TYPE_USER   = 1,    /**< 用户日志: 记录用户操作、配置变更等 */
    LOG_TYPE_ALARM  = 2     /**< 报警日志: 记录报警触发、异常事件等 */
} LogType;

/**
 * @brief 日志记录结构体
 * @details 用于存储单条日志的完整信息
 */
typedef struct {
    uint16_t year;                      /**< 年份 (如2026) */
    uint8_t  month;                     /**< 月份 (1-12) */
    uint8_t  day;                       /**< 日期 (1-31) */
    uint8_t  hour;                      /**< 小时 (0-23) */
    uint8_t  minute;                    /**< 分钟 (0-59) */
    uint8_t  second;                    /**< 秒 (0-59) */
    char     content[LOG_CONTENT_MAX_LEN];  /**< 日志内容字符串 */
} LogRecord;

/**
 * @brief 日志管理器配置结构体
 * @details 初始化时可传入自定义配置，NULL则使用默认配置
 */
typedef struct {
    uint8_t  enable;            /**< 日志记录使能: 1-启用 0-禁用 */
    uint16_t retention_days;   /**< 日志保留天数 (默认30天) */
} LogManagerConfig;

/**
 * @brief 日志查询过滤条件结构体
 * @details 用于指定查询的时间范围
 */
typedef struct {
    uint16_t start_year;            /**< 起始年份 */
    uint8_t  start_month;           /**< 起始月份 (1-12) */
    uint8_t  start_day;             /**< 起始日期 (1-31) */
    uint8_t  start_hour;            /**< 起始小时 (0-23) */
    uint8_t  start_min;             /**< 起始分钟 (0-59) */

    uint16_t end_year;              /**< 结束年份 */
    uint8_t  end_month;             /**< 结束月份 (1-12) */
    uint8_t  end_day;               /**< 结束日期 (1-31) */
    uint8_t  end_hour;             /**< 结束小时 (0-23) */
    uint8_t  end_min;               /**< 结束分钟 (0-59) */
} LogQueryFilter;

/**
 * @brief 日志查询回调函数类型
 * @param record 查询到的单条日志记录
 * @param user_data 用户传入的自定义数据
 * @note 遍历日志时，每匹配一条记录调用一次回调
 */
typedef void (*LogQueryCallback)(const LogRecord* record, void* user_data);

/**
 * @brief 日志管理器初始化
 * @param config 配置指针，传NULL使用默认配置
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 初始化文件系统、创建目录结构、设置默认参数
 * @attention 应在系统初始化时调用一次
 */
uint8_t log_manager_init(const LogManagerConfig* config);

/**
 * @brief 写入日志（自动获取当前时间）
 * @param type 日志类型 (LOG_TYPE_SYSTEM/USER/ALARM)
 * @param content 日志内容字符串
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 自动调用RTC获取当前时间后写入
 */
uint8_t log_write(LogType type, const char* content);

/**
 * @brief 写入日志（指定时间）
 * @param type 日志类型
 * @param year 年份
 * @param month 月份 (1-12)
 * @param day 日期 (1-31)
 * @param hour 小时 (0-23)
 * @param minute 分钟 (0-59)
 * @param second 秒 (0-59)
 * @param content 日志内容字符串
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 适用于需要记录历史时间或同步时间戳的场景
 */
uint8_t log_write_with_time(LogType type, uint16_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t minute, uint8_t second,
                            const char* content);

/**
 * @brief 查询日志记录
 * @param type 日志类型
 * @param filter 查询条件（传NULL表示查询全部）
 * @param callback 回调函数，每条记录调用一次
 * @param user_data 用户数据，传递给回调函数
 * @return 返回匹配的记录数
 * @note 支持按时间范围过滤查询
 */
uint32_t log_query(LogType type, const LogQueryFilter* filter,
                   LogQueryCallback callback, void* user_data);

/**
 * @brief 按日期查询日志
 * @param type 日志类型
 * @param year 年份
 * @param month 月份 (0表示全年)
 * @param day 日期 (0表示整月)
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 返回的记录数
 * @note 简化接口，内部构建时间范围过滤器
 */
uint32_t log_query_by_date(LogType type, uint16_t year, uint8_t month, uint8_t day,
                           LogQueryCallback callback, void* user_data);

/**
 * @brief 清理指定类型的过期日志
 * @param type 日志类型
 * @param keep_days 保留天数（最近N天的日志保留）
 * @return 删除的文件数
 * @note 删除该类型所有超过保留期的日志文件
 */
uint32_t log_cleanup(LogType type, uint16_t keep_days);

/**
 * @brief 清理所有类型的过期日志
 * @param keep_days 保留天数
 * @return 删除的文件总数
 * @note 依次清理SYSTEM/USER/ALARM三种日志
 */
uint32_t log_cleanup_all(uint16_t keep_days);

/**
 * @brief 获取指定类型日志的记录总数
 * @param type 日志类型
 * @return 记录总数
 * @note 统计自初始化以来的写入计数
 */
uint32_t log_get_count(LogType type);

/**
 * @brief 设置日志记录启用/禁用
 * @param enable 1:启用 0:禁用
 * @note 动态控制日志记录功能，不影响已写入日志
 */
void log_set_enable(uint8_t enable);

/**
 * @brief 强制刷新日志到存储介质
 * @return FILE_OK:成功
 * @note 确保数据已写入SD卡（写入时已同步，此函数直接返回）
 */
uint8_t log_flush(void);

/**
 * @brief 格式化（清空）指定类型的日志
 * @param type 日志类型
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 删除该类型所有日志文件并重置计数
 * @warning 此操作不可恢复，请谨慎使用
 */
uint8_t log_format(LogType type);

/**
 * @brief 格式化（清空）所有类型的日志
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 删除所有日志并重置所有计数
 * @warning 此操作不可恢复，请谨慎使用
 */
uint8_t log_format_all(void);

/**
 * @brief 关闭日志管理器
 * @note 刷新数据并清除初始化标志
 * @attention 系统关机或重启前应调用此函数
 */
void log_manager_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
