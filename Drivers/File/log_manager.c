/**
 * @file log_manager.c
 * @brief 日志管理器实现
 * @details 提供系统日志、用户操作日志、报警日志的记录和查询功能
 *          - 自动按日期分文件存储 (/LOGS/{SYS|USER|ALARM}/YYYY/MM/DD.log)
 *          - 支持历史日志查询
 *          - 支持日志清理和文件轮转
 * @note 该模块依赖 file_driver.c 提供的文件系统接口
 */

#include "log_manager.h"
#include "rtc_time.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief CSV文件头定义
 * @note 包含时间戳和日志内容两个字段
 */
static const char* g_log_header = "timestamp,content\r\n";

/**
 * @brief 日志管理器状态结构体
 * @details 保存全局配置、初始化状态和各类型日志的计数
 */
static struct {
    LogManagerConfig config;        /**< 日志管理器配置 */
    uint8_t initialized;           /**< 初始化标志: 0-未初始化 1-已初始化 */
    uint32_t sys_count;             /**< 系统日志记录总数 */
    uint32_t user_count;            /**< 用户操作日志记录总数 */
    uint32_t alarm_count;           /**< 报警日志记录总数 */
} g_log_manager = {0};

/**
 * @brief 默认配置
 * @note 当初始化传入NULL时使用此默认配置
 */
static const LogManagerConfig g_default_config = {
    .enable = 1,                   /**< 默认启用日志记录 */
    .retention_days = 30           /**< 默认保留30天 */
};

/**
 * @brief 获取日志类型对应的根目录路径
 * @param type 日志类型 (LOG_TYPE_SYSTEM/USER/ALARM)
 * @return 目录路径字符串
 * @note 内部函数，供路径构建使用
 */
static const char* get_log_path(LogType type)
{
    switch (type) {
        case LOG_TYPE_SYSTEM: return LOG_SYS_PATH;
        case LOG_TYPE_USER:   return LOG_USER_PATH;
        case LOG_TYPE_ALARM:  return LOG_ALARM_PATH;
        default:              return LOG_SYS_PATH;
    }
}

static uint8_t log_is_leap_year(uint16_t year)
{
    return (uint8_t)(((year % 4U) == 0U && (year % 100U) != 0U) ||
                     ((year % 400U) == 0U));
}

static uint8_t log_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {
        0U, 31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if (month == 2U) {
        return log_is_leap_year(year) ? 29U : 28U;
    }

    if (month >= 1U && month <= 12U) {
        return days[month];
    }

    return 31U;
}

static void log_subtract_days(uint16_t* year, uint8_t* month, uint8_t* day,
                              uint16_t days)
{
    while (days > 0U) {
        if (*day > 1U) {
            (*day)--;
        } else {
            if (*month > 1U) {
                (*month)--;
            } else {
                (*year)--;
                *month = 12U;
            }
            *day = log_days_in_month(*year, *month);
        }

        days--;
    }
}

/**
 * @brief 生成日志日文件路径
 * @param type 日志类型
 * @param year 年份
 * @param month 月份
 * @param day 日期
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @note 路径格式: /LOGS/{TYPE}/YYYY/MM/DD.log
 */
static void make_log_day_path(LogType type, uint16_t year, uint8_t month, uint8_t day,
                              char* buffer, uint32_t size)
{
    switch (type) {
        case LOG_TYPE_SYSTEM:
            snprintf(buffer, size, LOG_SYS_DAY_FILE, year, month, day);
            break;
        case LOG_TYPE_USER:
            snprintf(buffer, size, LOG_USER_DAY_FILE, year, month, day);
            break;
        case LOG_TYPE_ALARM:
            snprintf(buffer, size, LOG_ALARM_DAY_FILE, year, month, day);
            break;
        default:
            snprintf(buffer, size, LOG_SYS_DAY_FILE, year, month, day);
            break;
    }
}

/**
 * @brief 生成日志年目录路径
 * @param type 日志类型
 * @param year 年份
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @note 路径格式: /LOGS/{TYPE}/YYYY
 */
static void make_log_year_path(LogType type, uint16_t year, char* buffer, uint32_t size)
{
    switch (type) {
        case LOG_TYPE_SYSTEM:
            snprintf(buffer, size, LOG_SYS_YEAR_DIR, year);
            break;
        case LOG_TYPE_USER:
            snprintf(buffer, size, LOG_USER_YEAR_DIR, year);
            break;
        case LOG_TYPE_ALARM:
            snprintf(buffer, size, LOG_ALARM_YEAR_DIR, year);
            break;
        default:
            snprintf(buffer, size, LOG_SYS_YEAR_DIR, year);
            break;
    }
}

/**
 * @brief 生成日志月目录路径
 * @param type 日志类型
 * @param year 年份
 * @param month 月份
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @note 路径格式: /LOGS/{TYPE}/YYYY/MM
 */
static void make_log_month_path(LogType type, uint16_t year, uint8_t month,
                                char* buffer, uint32_t size)
{
    switch (type) {
        case LOG_TYPE_SYSTEM:
            snprintf(buffer, size, LOG_SYS_MONTH_DIR, year, month);
            break;
        case LOG_TYPE_USER:
            snprintf(buffer, size, LOG_USER_MONTH_DIR, year, month);
            break;
        case LOG_TYPE_ALARM:
            snprintf(buffer, size, LOG_ALARM_MONTH_DIR, year, month);
            break;
        default:
            snprintf(buffer, size, LOG_SYS_MONTH_DIR, year, month);
            break;
    }
}

/**
 * @brief 确保日志目录存在
 * @param type 日志类型
 * @param year 年份
 * @param month 月份
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 自动创建年份和月份目录
 */
static uint8_t ensure_log_dir(LogType type, uint16_t year, uint8_t month)
{
    char path[64];

    /* 创建年份目录: /LOGS/{TYPE}/YYYY */
    make_log_year_path(type, year, path, sizeof(path));
    if (!file_exists(path)) {
        if (file_create_dir(path) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    /* 创建月份目录: /LOGS/{TYPE}/YYYY/MM */
    make_log_month_path(type, year, month, path, sizeof(path));
    if (!file_exists(path)) {
        if (file_create_dir(path) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    return FILE_OK;
}

/**
 * @brief 写入日志文件头
 * @param type 日志类型
 * @param year 年份
 * @param month 月份
 * @param day 日期
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 仅在新文件创建时调用，写入CSV头行
 */
static uint8_t write_log_header(LogType type, uint16_t year, uint8_t month, uint8_t day)
{
    char filepath[64];
    uint8_t result;

    make_log_day_path(type, year, month, day, filepath, sizeof(filepath));

    result = file_open(filepath, FILE_MODE_WRITE);
    if (result != FILE_OK) {
        return FILE_ERROR;
    }

    result = file_write(g_log_header, strlen(g_log_header), NULL);
    file_close();

    return result;
}

/**
 * @brief 写入单条日志记录
 * @param type 日志类型
 * @param year 年份
 * @param month 月份
 * @param day 日期
 * @param record 日志记录结构体
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 自动检查文件是否存在，不存在则创建目录和头文件
 */
static uint8_t write_log_record(LogType type, uint16_t year, uint8_t month, uint8_t day,
                                const LogRecord* record)
{
    char filepath[64];      /**< 文件路径 */
    char buffer[256];       /**< 行缓冲区 */
    uint32_t len;           /**< 实际写入长度 */
    uint8_t result;         /**< 操作结果 */

    make_log_day_path(type, year, month, day, filepath, sizeof(filepath));

    /* 检查文件是否存在，不存在则创建 */
    if (!file_exists(filepath)) {
        if (ensure_log_dir(type, year, month) != FILE_OK) {
            return FILE_ERROR;
        }
        if (write_log_header(type, year, month, day) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    /* 构建日志行: YYYY-MM-DD HH:MM:SS,content */
    len = snprintf(buffer, sizeof(buffer),
                   "%04u-%02u-%02u %02u:%02u:%02u,%s\r\n",
                   record->year, record->month, record->day,
                   record->hour, record->minute, record->second,
                   record->content);

    /* 追加写入文件 */
    result = file_open(filepath, FILE_MODE_APPEND);
    if (result != FILE_OK) {
        return FILE_ERROR;
    }

    result = file_write(buffer, len, NULL);
    file_close();

    if (result != FILE_OK) {
        return FILE_ERROR;
    }

    return FILE_OK;
}

/**
 * @brief 获取当前时间
 * @param year 输出: 年份
 * @param month 输出: 月份
 * @param day 输出: 日期
 * @param hour 输出: 小时
 * @param minute 输出: 分钟
 * @param second 输出: 秒
 * @note 实际项目中应从RTC获取真实时间
 */
static void get_current_time(uint16_t* year, uint8_t* month, uint8_t* day,
                             uint8_t* hour, uint8_t* minute, uint8_t* second)
{
    RTC_TimeData timeData;
    RTC_Time_Get(&timeData);
    *year = timeData.year;
    *month = timeData.month;
    *day = timeData.date;
    *hour = timeData.hour;
    *minute = timeData.minute;
    *second = timeData.second;
}

/**
 * @brief 确保基础目录结构存在
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 创建 /LOGS 根目录及 SYS/USER/ALARM 三个子目录
 */
static uint8_t ensure_base_dirs(void)
{
    /* 创建根目录: /LOGS */
    if (!file_exists(LOG_BASE_PATH)) {
        if (file_create_dir(LOG_BASE_PATH) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    /* 创建系统日志目录: /LOGS/SYS */
    if (!file_exists(LOG_SYS_PATH)) {
        if (file_create_dir(LOG_SYS_PATH) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    /* 创建用户日志目录: /LOGS/USER */
    if (!file_exists(LOG_USER_PATH)) {
        if (file_create_dir(LOG_USER_PATH) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    /* 创建报警日志目录: /LOGS/ALARM */
    if (!file_exists(LOG_ALARM_PATH)) {
        if (file_create_dir(LOG_ALARM_PATH) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    return FILE_OK;
}

/**
 * @brief 日志管理器初始化
 * @param config 配置指针，传NULL使用默认配置
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 初始化文件系统、创建目录结构、设置默认参数
 */
uint8_t log_manager_init(const LogManagerConfig* config)
{
    /* 检查是否已初始化，避免重复初始化 */
    if (g_log_manager.initialized) {
        return FILE_OK;
    }

    /* 复制配置参数 */
    if (config) {
        memcpy(&g_log_manager.config, config, sizeof(LogManagerConfig));
    } else {
        memcpy(&g_log_manager.config, &g_default_config, sizeof(LogManagerConfig));
    }

    /* 初始化文件系统 */
    file_init();

    /* 确保基础目录创建成功，否则禁用日志 */
    if (ensure_base_dirs() != FILE_OK) {
        g_log_manager.config.enable = 0;
    }

    /* 设置初始化状态和计数清零 */
    g_log_manager.initialized = 1;
    g_log_manager.sys_count = 0;
    g_log_manager.user_count = 0;
    g_log_manager.alarm_count = 0;

    return FILE_OK;
}

/**
 * @brief 写入日志（自动获取当前时间）
 * @param type 日志类型
 * @param content 日志内容
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 自动调用 get_current_time() 获取当前时间
 */
uint8_t log_write(LogType type, const char* content)
{
    uint16_t year;          /**< 年份 */
    uint8_t month, day;     /**< 月份、日期 */
    uint8_t hour, minute, second;  /**< 时分秒 */

    get_current_time(&year, &month, &day, &hour, &minute, &second);

    return log_write_with_time(type, year, month, day, hour, minute, second, content);
}

/**
 * @brief 写入日志（指定时间）
 * @param type 日志类型
 * @param year 年份
 * @param month 月份
 * @param day 日期
 * @param hour 小时
 * @param minute 分钟
 * @param second 秒
 * @param content 日志内容
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 实际写入接口，内部调用 write_log_record()
 */
uint8_t log_write_with_time(LogType type, uint16_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t minute, uint8_t second,
                            const char* content)
{
    LogRecord record;       /**< 日志记录结构体 */

    /* 检查初始化状态和启用标志 */
    if (!g_log_manager.initialized || !g_log_manager.config.enable) {
        return FILE_ERROR;
    }

    /* 填充记录字段 */
    record.year = year;
    record.month = month;
    record.day = day;
    record.hour = hour;
    record.minute = minute;
    record.second = second;
    strncpy(record.content, content, LOG_CONTENT_MAX_LEN - 1);
    record.content[LOG_CONTENT_MAX_LEN - 1] = '\0';

    /* 写入日志文件 */
    if (write_log_record(type, year, month, day, &record) != FILE_OK) {
        return FILE_ERROR;
    }

    /* 更新对应类型日志的计数 */
    switch (type) {
        case LOG_TYPE_SYSTEM:
            g_log_manager.sys_count++;
            break;
        case LOG_TYPE_USER:
            g_log_manager.user_count++;
            break;
        case LOG_TYPE_ALARM:
            g_log_manager.alarm_count++;
            break;
    }

    return FILE_OK;
}

/**
 * @brief 解析日志行为日志记录结构
 * @param line 日志行字符串
 * @param record 输出记录结构
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 解析格式: YYYY-MM-DD HH:MM:SS,content
 */
static uint8_t parse_log_line(const char* line, LogRecord* record)
{
    int ret;                    /**< sscanf 返回值 */
    int year, month, day;       /**< 时间字段 */
    int hour, minute, second;
    char* comma_pos;            /**< 逗号位置指针 */

    if (!line || !record) {
        return FILE_ERROR;
    }

    /* 解析时间戳部分 */
    ret = sscanf(line, "%d-%d-%d %d:%d:%d",
                 &year, &month, &day, &hour, &minute, &second);

    if (ret != 6) {
        return FILE_ERROR;
    }

    /* 填充时间字段 */
    record->year = (uint16_t)year;
    record->month = (uint8_t)month;
    record->day = (uint8_t)day;
    record->hour = (uint8_t)hour;
    record->minute = (uint8_t)minute;
    record->second = (uint8_t)second;

    /* 提取逗号后的内容部分 */
    comma_pos = strchr(line, ',');
    if (comma_pos) {
        strncpy(record->content, comma_pos + 1, LOG_CONTENT_MAX_LEN - 1);
        record->content[LOG_CONTENT_MAX_LEN - 1] = '\0';

        /* 去除换行符 */
        char* newline = strchr(record->content, '\r');
        if (newline) *newline = '\0';
        newline = strchr(record->content, '\n');
        if (newline) *newline = '\0';
    } else {
        record->content[0] = '\0';
    }

    return FILE_OK;
}

/**
 * @brief 查询指定日期的日志文件
 * @param type 日志类型
 * @param year 年份
 * @param month 月份
 * @param day 日期
 * @param filter 查询条件（可为NULL表示全部）
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 返回的记录数
 * @note 内部函数，逐行读取并解析日志文件
 */
static uint32_t query_day_file(LogType type, uint16_t year, uint8_t month, uint8_t day,
                               const LogQueryFilter* filter,
                               LogQueryCallback callback, void* user_data)
{
    char filepath[64];              /**< 文件路径 */
    char buffer[256];                /**< 读取缓冲区 */
    LogRecord record;                /**< 日志记录 */
    uint32_t count = 0;             /**< 匹配计数 */
    uint32_t bytes_read;            /**< 实际读取字节数 */
    uint8_t result;                 /**< 操作结果 */

    make_log_day_path(type, year, month, day, filepath, sizeof(filepath));

    /* 文件不存在则直接返回 */
    if (!file_exists(filepath)) {
        return 0;
    }

    /* 打开文件读取 */
    result = file_open(filepath, FILE_MODE_READ);
    if (result != FILE_OK) {
        return 0;
    }

    /* 跳过CSV头行 */
    file_read(buffer, strlen(g_log_header), &bytes_read);

    /* 逐行读取解析 */
    while (1) {
        result = file_read(buffer, sizeof(buffer) - 1, &bytes_read);

        if (result != FILE_OK || bytes_read == 0) {
            break;
        }

        buffer[bytes_read] = '\0';

        /* 提取完整行 */
        char* line_end = strchr(buffer, '\n');
        if (line_end) {
            line_end[0] = '\0';

            /* 解析日志行 */
            if (parse_log_line(buffer, &record) == FILE_OK) {
                uint8_t pass = 1;  /**< 是否通过过滤条件 */

                /* 应用时间过滤条件 */
                if (filter) {
                    uint32_t record_time = record.hour * 100 + record.minute;
                    uint32_t start_time = filter->start_hour * 100 + filter->start_min;
                    uint32_t end_time = filter->end_hour * 100 + filter->end_min;

                    if (record_time < start_time || record_time > end_time) {
                        pass = 0;
                    }
                }

                /* 通过过滤则调用回调函数 */
                if (pass) {
                    if (callback) {
                        callback(&record, user_data);
                    }
                    count++;
                }
            }
        }
    }

    file_close();
    return count;
}

/**
 * @brief 查询日志记录
 * @param type 日志类型
 * @param filter 查询条件（可为NULL表示全部）
 * @param callback 回调函数，每条记录调用一次
 * @param user_data 用户数据，传递给回调函数
 * @return 返回的记录数
 * @note 遍历日期范围内的所有文件并查询
 */
uint32_t log_query(LogType type, const LogQueryFilter* filter,
                   LogQueryCallback callback, void* user_data)
{
    uint32_t count = 0;         /**< 总记录数 */
    uint16_t year, month, day;  /**< 循环变量 */
    char filepath[64];          /**< 路径缓冲区 */

    if (!g_log_manager.initialized) {
        return 0;
    }

    /* 未指定日期范围则查询所有 */
    if (!filter || (filter->start_year == 0 && filter->end_year == 0)) {
        uint16_t start_year = 2024;
        uint16_t end_year = 2030;
        /* 获取当前年份作为结束年份 */
        RTC_TimeData timeData;
        RTC_Time_Get(&timeData);
        end_year = timeData.year;
        for (year = start_year; year <= end_year; year++) {
            make_log_year_path(type, year, filepath, sizeof(filepath));
            if (!file_exists(filepath)) {
                if (year == 2024) continue;
                break;
            }

            for (month = 1; month <= 12; month++) {
                make_log_month_path(type, year, month, filepath, sizeof(filepath));
                if (!file_exists(filepath)) {
                    break;
                }

                for (day = 1; day <= 31; day++) {
                    make_log_day_path(type, year, month, day, filepath, sizeof(filepath));
                    if (!file_exists(filepath)) {
                        continue;
                    }
                    count += query_day_file(type, year, month, day, filter, callback, user_data);
                }
            }
        }
    } else {
        /* 按指定范围查询 */
        for (year = filter->start_year; year <= filter->end_year; year++) {
            uint8_t start_m = (year == filter->start_year) ? filter->start_month : 1;
            uint8_t end_m = (year == filter->end_year) ? filter->end_month : 12;

            for (month = start_m; month <= end_m; month++) {
                uint8_t start_d = (year == filter->start_year && month == filter->start_month) ? filter->start_day : 1;
                uint8_t end_d = (year == filter->end_year && month == filter->end_month) ? filter->end_day : 31;

                for (day = start_d; day <= end_d; day++) {
                    count += query_day_file(type, year, month, day, filter, callback, user_data);
                }
            }
        }
    }

    return count;
}

/**
 * @brief 按日期查询日志
 * @param type 日志类型
 * @param year 年份
 * @param month 月份(0表示全年)
 * @param day 日期(0表示整月)
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 返回的记录数
 * @note 内部构建时间范围过滤器并调用 log_query()
 */
uint32_t log_query_by_date(LogType type, uint16_t year, uint8_t month, uint8_t day,
                           LogQueryCallback callback, void* user_data)
{
    LogQueryFilter filter = {0};

    filter.start_year = year;
    filter.start_month = (month == 0) ? 1 : month;
    filter.start_day = (day == 0) ? 1 : day;
    filter.start_hour = 0;
    filter.start_min = 0;

    filter.end_year = year;
    filter.end_month = (month == 0) ? 12 : month;
    filter.end_day = (day == 0) ? 31 : day;
    filter.end_hour = 23;
    filter.end_min = 59;

    return log_query(type, &filter, callback, user_data);
}

/**
 * @brief 清理指定类型的过期日志
 * @param type 日志类型
 * @param keep_days 保留天数
 * @return 删除的文件数
 * @note 内部函数，计算截止日期并删除该日期之前的文件
 */
static uint32_t cleanup_type(LogType type, uint16_t keep_days)
{
    uint32_t delete_count = 0;     /**< 删除计数 */
    char filepath[64];             /**< 路径缓冲区 */
    uint16_t y;                    /**< 年循环变量 */
    uint8_t m, d;                  /**< 月日循环变量 */
    uint16_t cutoff_year;          /**< 截止年份 */
    uint8_t cutoff_month;          /**< 截止月份 */
    uint8_t cutoff_day;            /**< 截止日期 */

    /* 从RTC获取当前时间 */
    RTC_TimeData timeData;
    RTC_Time_Get(&timeData);
    cutoff_year = timeData.year;
    cutoff_month = timeData.month;
    cutoff_day = timeData.date;

    /* 限制保留天数范围 */
    if (keep_days > LOG_MAX_RETENTION_DAYS) {
        keep_days = LOG_MAX_RETENTION_DAYS;
    }

    /* 计算截止日期（从当前日期往前推keep_days天） */
    log_subtract_days(&cutoff_year, &cutoff_month, &cutoff_day, keep_days);

    /* 遍历所有年份 */
    for (y = 2024; y <= cutoff_year; y++) {
        make_log_year_path(type, y, filepath, sizeof(filepath));
        if (!file_exists(filepath)) {
            if (y == 2024) continue;
            break;
        }

        /* 遍历所有月份 */
        for (m = 1; m <= 12; m++) {
            /* 超过截止月份则停止 */
            if (y == cutoff_year && m > cutoff_month) {
                break;
            }

            /* 截止月份：只删除截止日期之前的 */
            if (y == cutoff_year && m == cutoff_month) {
                for (d = 1; d < cutoff_day; d++) {
                    make_log_day_path(type, y, m, d, filepath, sizeof(filepath));
                    if (file_exists(filepath)) {
                        file_delete(filepath);
                        delete_count++;
                    }
                }
            }
            /* 早于截止月份的整月删除 */
            else if (y < cutoff_year || (y == cutoff_year && m < cutoff_month)) {
                for (d = 1; d <= 31; d++) {
                    make_log_day_path(type, y, m, d, filepath, sizeof(filepath));
                    if (file_exists(filepath)) {
                        file_delete(filepath);
                        delete_count++;
                    }
                }
            }
        }
    }

    return delete_count;
}

/**
 * @brief 清理指定类型的过期日志
 * @param type 日志类型
 * @param keep_days 保留天数
 * @return 删除的文件数
 * @note 公开接口，内部调用 cleanup_type()
 */
uint32_t log_cleanup(LogType type, uint16_t keep_days)
{
    if (!g_log_manager.initialized) {
        return 0;
    }

    return cleanup_type(type, keep_days);
}

/**
 * @brief 清理所有类型的过期日志
 * @param keep_days 保留天数
 * @return 删除的文件总数
 * @note 依次清理 SYSTEM/USER/ALARM 三种日志
 */
uint32_t log_cleanup_all(uint16_t keep_days)
{
    uint32_t count = 0;

    if (!g_log_manager.initialized) {
        return 0;
    }

    count += cleanup_type(LOG_TYPE_SYSTEM, keep_days);
    count += cleanup_type(LOG_TYPE_USER, keep_days);
    count += cleanup_type(LOG_TYPE_ALARM, keep_days);

    return count;
}

/**
 * @brief 获取指定类型日志的记录总数
 * @param type 日志类型
 * @return 记录总数
 * @note 统计自初始化以来的写入计数
 */
uint32_t log_get_count(LogType type)
{
    switch (type) {
        case LOG_TYPE_SYSTEM: return g_log_manager.sys_count;
        case LOG_TYPE_USER:   return g_log_manager.user_count;
        case LOG_TYPE_ALARM:  return g_log_manager.alarm_count;
        default:              return 0;
    }
}

/**
 * @brief 设置日志记录启用/禁用
 * @param enable 1:启用 0:禁用
 * @note 动态控制日志记录功能
 */
void log_set_enable(uint8_t enable)
{
    g_log_manager.config.enable = enable ? 1 : 0;
}

/**
 * @brief 强制刷新日志到存储介质
 * @return FILE_OK:成功
 * @note 由于写入时已同步，此函数直接返回成功
 */
uint8_t log_flush(void)
{
    return FILE_OK;
}

/**
 * @brief 格式化（清空）指定类型的日志
 * @param type 日志类型
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 删除该类型所有日志文件并重置计数
 */
static uint8_t format_type(LogType type)
{
    char filepath[64];      /**< 路径缓冲区 */
    uint16_t y;              /**< 年循环变量 */
    uint8_t m, d;            /**< 月日循环变量 */

    /* 获取当前年份 */
    RTC_TimeData timeData;
    RTC_Time_Get(&timeData);

    /* 遍历所有日期文件并删除 */
    for (y = 2024; y <= timeData.year; y++) {
        for (m = 1; m <= 12; m++) {
            for (d = 1; d <= 31; d++) {
                make_log_day_path(type, y, m, d, filepath, sizeof(filepath));
                if (file_exists(filepath)) {
                    file_delete(filepath);
                }
            }

            /* 删除月份目录 */
            make_log_month_path(type, y, m, filepath, sizeof(filepath));
            if (file_exists(filepath)) {
                file_delete(filepath);
            }
        }

        /* 删除年份目录 */
        make_log_year_path(type, y, filepath, sizeof(filepath));
        if (file_exists(filepath)) {
            file_delete(filepath);
        }
    }

    return FILE_OK;
}

/**
 * @brief 格式化（清空）指定类型的日志
 * @param type 日志类型
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 公开接口，删除所有日志并重置计数
 */
uint8_t log_format(LogType type)
{
    if (!g_log_manager.initialized) {
        return FILE_ERROR;
    }

    format_type(type);

    /* 重置对应类型的计数 */
    switch (type) {
        case LOG_TYPE_SYSTEM:
            g_log_manager.sys_count = 0;
            break;
        case LOG_TYPE_USER:
            g_log_manager.user_count = 0;
            break;
        case LOG_TYPE_ALARM:
            g_log_manager.alarm_count = 0;
            break;
    }

    return FILE_OK;
}

/**
 * @brief 格式化（清空）所有类型的日志
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 删除所有日志并重置所有计数
 */
uint8_t log_format_all(void)
{
    if (!g_log_manager.initialized) {
        return FILE_ERROR;
    }

    format_type(LOG_TYPE_SYSTEM);
    format_type(LOG_TYPE_USER);
    format_type(LOG_TYPE_ALARM);

    g_log_manager.sys_count = 0;
    g_log_manager.user_count = 0;
    g_log_manager.alarm_count = 0;

    return FILE_OK;
}

/**
 * @brief 关闭日志管理器
 * @note 刷新数据并清除初始化标志
 */
void log_manager_deinit(void)
{
    if (!g_log_manager.initialized) {
        return;
    }

    log_flush();
    g_log_manager.initialized = 0;
}
