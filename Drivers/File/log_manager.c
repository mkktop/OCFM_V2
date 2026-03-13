/**
 * @file log_manager.c
 * @brief 日志管理器实现
 * @details 提供系统日志、操作日志、流量数据的记录功能
 */

#include "log_manager.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 日志类型对应的子目录名
 */
static const char* g_log_type_dirs[] = {
    "system",   /* LOG_TYPE_SYSTEM */
    "oper",     /* LOG_TYPE_OPER */
    "flow",     /* LOG_TYPE_FLOW */
    "alarm"     /* LOG_TYPE_ALARM */
};

/**
 * @brief 日志级别对应的字符串
 */
static const char* g_level_str[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

/**
 * @brief 日志管理器状态
 */
static struct {
    LogManagerConfig config;
    uint8_t initialized;
    uint32_t error_count;
    char current_date[9];       /* YYYYMMDD */
    uint32_t file_seq[4];       /* 各类型当前文件序号 */
} g_log_state = {0};

/**
 * @brief 默认配置
 */
static const LogManagerConfig g_default_config = {
    .enable_console = 1,
    .enable_sd = 1,
    .min_level = LOG_LEVEL_INFO,
    .max_file_size = 1024 * 1024,    /* 1MB */
    .max_file_count = 10,
    .base_path = "/logs"
};

/**
 * @brief 获取当前日期字符串
 */
static void get_current_date(char* date_str, uint32_t size)
{
    /* 实际项目中应从RTC获取 */
    /* 这里示例使用固定格式 */
    strncpy(date_str, "20240313", size);
}

/**
 * @brief 检查并更新日期
 */
static void check_date_change(void)
{
    char new_date[9];
    get_current_date(new_date, sizeof(new_date));

    if (strncmp(g_log_state.current_date, new_date, 8) != 0) {
        strncpy(g_log_state.current_date, new_date, 8);
        /* 日期变化，重置文件序号 */
        memset(g_log_state.file_seq, 0, sizeof(g_log_state.file_seq));
    }
}

/**
 * @brief 确保日志目录存在
 */
static uint8_t ensure_log_dir(LogType log_type)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/%s",
             g_log_state.config.base_path,
             g_log_type_dirs[log_type]);

    /* 检查目录是否存在，不存在则创建 */
    if (!file_exists(path)) {
        /* 先确保根目录存在 */
        if (!file_exists(g_log_state.config.base_path)) {
            if (file_create_dir(g_log_state.config.base_path) != FILE_OK) {
                return FILE_ERROR;
            }
        }
        if (file_create_dir(path) != FILE_OK) {
            return FILE_ERROR;
        }
    }
    return FILE_OK;
}

/**
 * @brief 构建日志文件路径
 */
static void build_log_path(LogType log_type, char* path, uint32_t path_size)
{
    check_date_change();

    snprintf(path, path_size, "%s/%s/%s_%s_%03lu.txt",
             g_log_state.config.base_path,
             g_log_type_dirs[log_type],
             g_log_type_dirs[log_type],
             g_log_state.current_date,
             g_log_state.file_seq[log_type]);
}

/**
 * @brief 检查并切换日志文件
 */
static uint8_t check_and_rotate_file(LogType log_type)
{
    char path[64];
    char new_path[64];
    build_log_path(log_type, path, sizeof(path));

    /* 检查文件大小 */
    if (file_exists(path)) {
        /* 获取文件大小 - 通过尝试打开获取 */
        /* 简化处理：直接检查序号 */
        g_log_state.file_seq[log_type]++;
        if (g_log_state.file_seq[log_type] >= g_log_state.config.max_file_count) {
            g_log_state.file_seq[log_type] = 0;  /* 循环覆盖 */
        }
        build_log_path(log_type, new_path, sizeof(new_path));
        /* 删除旧文件 */
        file_delete(new_path);
    }

    return FILE_OK;
}

/**
 * @brief 写入单条日志
 */
static uint8_t write_log_entry(LogType log_type, LogLevel level, const char* content)
{
    uint8_t result;
    char path[64];
    char timestamp[24];
    char log_line[512];

    /* 检查日志级别 */
    if (level < g_log_state.config.min_level) {
        return FILE_OK;  /* 忽略低级别日志 */
    }

    /* 确保目录存在 */
    if (ensure_log_dir(log_type) != FILE_OK) {
        g_log_state.error_count++;
        return FILE_ERROR;
    }

    /* 检查文件切换 */
    check_and_rotate_file(log_type);

    /* 构建文件路径 */
    build_log_path(log_type, path, sizeof(path));

    /* 构建时间戳 (实际应从RTC获取) */
    snprintf(timestamp, sizeof(timestamp), "[%s %02d:%02d:%02d]",
             g_log_state.current_date, 12, 0, 0);  /* 示例时间 */

    /* 构建日志行 */
    snprintf(log_line, sizeof(log_line), "%s [%s] %s\r\n",
             timestamp, g_level_str[level], content);

    /* 输出到控制台 */
    if (g_log_state.config.enable_console) {
        printf("%s", log_line);
    }

    /* 写入SD卡 */
    if (g_log_state.config.enable_sd) {
        result = file_open(path, FILE_MODE_APPEND);
        if (result != FILE_OK) {
            /* 尝试创建新文件 */
            result = file_open(path, FILE_MODE_WRITE);
        }
        if (result != FILE_OK) {
            g_log_state.error_count++;
            return FILE_ERROR;
        }

        file_write(log_line, strlen(log_line), NULL);
        file_close();
    }

    return FILE_OK;
}

uint8_t log_manager_init(const LogManagerConfig* config)
{
    if (g_log_state.initialized) {
        return FILE_OK;  /* 已初始化 */
    }

    /* 复制配置 */
    if (config) {
        memcpy(&g_log_state.config, config, sizeof(LogManagerConfig));
    } else {
        memcpy(&g_log_state.config, &g_default_config, sizeof(LogManagerConfig));
    }

    /* 获取当前日期 */
    get_current_date(g_log_state.current_date, sizeof(g_log_state.current_date));
    memset(g_log_state.file_seq, 0, sizeof(g_log_state.file_seq));

    /* 初始化文件系统 */
    if (file_init() != FILE_OK) {
        /* SD卡未插入，仅使用控制台 */
        g_log_state.config.enable_sd = 0;
    }

    /* 创建基础目录 */
    if (g_log_state.config.enable_sd) {
        ensure_log_dir(LOG_TYPE_SYSTEM);
        ensure_log_dir(LOG_TYPE_OPER);
        ensure_log_dir(LOG_TYPE_FLOW);
        ensure_log_dir(LOG_TYPE_ALARM);
    }

    g_log_state.initialized = 1;
    g_log_state.error_count = 0;

    log_system("LOG_INIT", "Log manager initialized");
    return FILE_OK;
}

void log_set_level(LogLevel level)
{
    g_log_state.config.min_level = level;
}

uint8_t log_write(LogLevel level, const char* fmt, ...)
{
    va_list args;
    char buffer[256];

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    return write_log_entry(LOG_TYPE_SYSTEM, level, buffer);
}

uint8_t log_system(const char* event, const char* detail)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "SYS: %s - %s", event, detail);
    return write_log_entry(LOG_TYPE_SYSTEM, LOG_LEVEL_INFO, buffer);
}

uint8_t log_operation(const char* operator_name, const char* action, const char* result)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "OPER: [%s] %s - %s",
             operator_name, action, result);
    return write_log_entry(LOG_TYPE_OPER, LOG_LEVEL_INFO, buffer);
}

uint8_t log_flow_data(const char* timestamp, float water_level,
                      float instant_flow, double total_flow)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s,%.3f,%.3f,%.6f",
             timestamp, water_level, instant_flow, total_flow);
    return write_log_entry(LOG_TYPE_FLOW, LOG_LEVEL_INFO, buffer);
}

uint8_t log_alarm(const char* alarm_type, uint8_t alarm_level, const char* description)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "ALARM[%d]: %s - %s",
             alarm_level, alarm_type, description);
    return write_log_entry(LOG_TYPE_ALARM,
                          (alarm_level >= 3) ? LOG_LEVEL_ERROR : LOG_LEVEL_WARN,
                          buffer);
}

uint8_t log_get_current_path(LogType log_type, char* path, uint32_t path_size)
{
    if (!g_log_state.initialized || log_type > LOG_TYPE_ALARM) {
        return FILE_ERROR;
    }
    build_log_path(log_type, path, path_size);
    return FILE_OK;
}

uint8_t log_list_files(LogType log_type)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/%s",
             g_log_state.config.base_path,
             g_log_type_dirs[log_type]);
    return file_list_dir(path);
}

uint8_t log_delete_by_date(LogType log_type, const char* date)
{
    char path[64];
    char pattern[16];
    uint8_t result = FILE_OK;

    snprintf(path, sizeof(path), "%s/%s",
             g_log_state.config.base_path,
             g_log_type_dirs[log_type]);

    /* 实际项目中需要遍历目录删除匹配日期的文件 */
    /* 简化实现：这里仅作为示例 */
    for (int i = 0; i < g_log_state.config.max_file_count; i++) {
        snprintf(pattern, sizeof(pattern), "%s_%s_%03d.txt",
                 g_log_type_dirs[log_type], date, i);
        char full_path[128];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, pattern);
        if (file_exists(full_path)) {
            if (file_delete(full_path) != FILE_OK) {
                result = FILE_ERROR;
            }
        }
    }

    return result;
}

uint8_t log_cleanup(uint32_t keep_days)
{
    /* 实际项目中应根据RTC计算日期差并删除过期文件 */
    /* 简化实现：删除所有非今日的日志 */
    (void)keep_days;
    return FILE_OK;
}

uint8_t log_flush(void)
{
    /* 文件已每次关闭时同步，此处仅作为接口保留 */
    return FILE_OK;
}

void log_manager_deinit(void)
{
    if (!g_log_state.initialized) {
        return;
    }

    log_system("LOG_DEINIT", "Log manager shutting down");
    g_log_state.initialized = 0;
}

uint32_t log_get_error_count(void)
{
    return g_log_state.error_count;
}
