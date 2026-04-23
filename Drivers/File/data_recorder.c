/**
 * @file data_recorder.c
 * @brief 数据记录器实现
 * @details 提供流量历史数据的CSV格式记录和查询功能
 *          - 自动按日期分文件存储 (/data/YYYY/MM/DD.csv)
 *          - 按时间间隔自动记录流量数据
 *          - 支持历史数据查询和导出
 *          - 支持数据清理和文件轮转
 */

#include "data_recorder.h"
#include "rtc_time.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief CSV文件头定义
 * @note 包含时间戳、水位、瞬时流量、累计流量、累计时间、温度、标志位
 */
static const char* g_csv_header = "timestamp,water_level,instant_flow,total_flow,total_time,temperature,alarm_flags\r\n";

/**
 * @brief 数据记录器状态
 */
static struct {
    DataRecorderConfig config;          /**< 记录器配置 */
    uint8_t initialized;                /**< 初始化标志 */
    uint32_t record_count;              /**< 已记录的数据条数 */
    uint32_t file_size;                 /**< 当前文件大小 */
    uint32_t next_record_time;          /**< 下次记录时间戳 */
    char current_date[9];               /**< 当前日期字符串(YYYYMMDD) */
} g_recorder = {0};

/**
 * @brief 默认配置
 * @note 当初始化传入NULL时使用此默认配置
 */
static const DataRecorderConfig g_default_config = {
    .enable = 1,
    .interval_sec = DATA_DEFAULT_INTERVAL,    /**< 默认5分钟记录一次 */
    .retention_days = 30,                      /**< 默认保留30天 */
    .csv_header = 1,                           /**< 写入CSV头 */
    .max_file_size = 10 * 1024 * 1024          /**< 默认10MB (单日文件大小) */
};

/**
 * @brief 根据日期生成文件路径
 * @param year 年
 * @param month 月
 * @param day 日
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 文件路径字符串
 */
static char* make_day_path(uint16_t year, uint8_t month, uint8_t day, char* buffer, uint32_t size)
{
    snprintf(buffer, size, DATA_DAY_FILE, year, month, day);
    return buffer;
}

/**
 * @brief 确保日期目录存在
 * @param year 年
 * @param month 月
 * @return FILE_OK:成功 FILE_ERROR:失败
 */
static uint8_t ensure_date_dir(uint16_t year, uint8_t month)
{
    char path[64];

    /* 创建年份目录 /data/YYYY */
    snprintf(path, sizeof(path), DATA_YEAR_DIR, year);
    if (!file_exists(path)) {
        if (file_create_dir(path) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    /* 创建月份目录 /data/YYYY/MM */
    snprintf(path, sizeof(path), DATA_MONTH_DIR, year, month);
    if (!file_exists(path)) {
        if (file_create_dir(path) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    return FILE_OK;
}

/**
 * @brief 写入CSV文件头
 * @param year 年
 * @param month 月
 * @param day 日
 * @return FILE_OK:成功 FILE_ERROR:失败
 */
static uint8_t write_day_header(uint16_t year, uint8_t month, uint8_t day)
{
    char filepath[64];
    uint8_t result;

    make_day_path(year, month, day, filepath, sizeof(filepath));// 生成日期文件路径

    result = file_open(filepath, FILE_MODE_WRITE);
    if (result != FILE_OK) {
        return FILE_ERROR;
    }

    result = file_write(g_csv_header, strlen(g_csv_header), NULL);// 写入CSV头
    file_close();

    return result;
}

/**
 * @brief 记录数据到指定日期文件
 * @param year 年
 * @param month 月
 * @param day 日
 * @param record 数据记录
 * @return FILE_OK:成功 FILE_ERROR:失败
 */
static uint8_t write_day_record(uint16_t year, uint8_t month, uint8_t day, const DataRecord* record)
{
    char filepath[64];// 日期文件路径
    char buffer[RECORD_LINE_MAX_LEN];// 记录行缓冲区
    uint32_t len;// 记录行长度
    uint8_t result;// 写入结果
    // 生成日期文件路径
    make_day_path(year, month, day, filepath, sizeof(filepath));

    /* 检查文件是否存在，不存在则创建 */
    if (!file_exists(filepath)) {
        if (ensure_date_dir(year, month) != FILE_OK) {
            return FILE_ERROR;
        }
        if (write_day_header(year, month, day) != FILE_OK) {
            return FILE_ERROR;
        }
    }

    /* 构建CSV行 */
    len = snprintf(buffer, sizeof(buffer),
                   "%04u-%02u-%02u %02u:%02u:%02u,%.3f,%.6f,%.6f,%lu,%.1f,%u\r\n",
                   record->year, record->month, record->day,
                   record->hour, record->minute, record->second,
                   record->water_level,
                   record->instant_flow,
                   record->total_flow,
                   record->total_time,
                   record->temperature,
                   record->flags);

    /* 追加写入 */
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
 * @brief 获取当前时间戳
 * @return Unix时间戳
 * @note 从RTC获取当前时间并转换为Unix时间戳
 */
static uint32_t get_timestamp(void)
{
    return RTC_Time_GetTimestamp();
}

/**
 * @brief 数据记录器初始化
 * @param config 配置指针，传NULL使用默认配置
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 初始化文件系统、创建数据目录
 */
uint8_t data_recorder_init(const DataRecorderConfig* config)
{
    // 检查是否已初始化，若已初始化则跳过
    if (g_recorder.initialized) {
        return FILE_OK;
    }
    /* 复制配置 */
    if (config) {
        memcpy(&g_recorder.config, config, sizeof(DataRecorderConfig));
    } else {
        memcpy(&g_recorder.config, &g_default_config, sizeof(DataRecorderConfig));
    }

    /* 初始化文件系统 */
    file_init();

    /* 确保根目录存在 */
    if (!file_exists(DATA_BASE_PATH)) {
        if (file_create_dir(DATA_BASE_PATH) != FILE_OK) {
            g_recorder.config.enable = 0;  /* SD卡不可用，禁用记录 */
        }
    }

    g_recorder.initialized = 1;
    g_recorder.next_record_time = get_timestamp() + g_recorder.config.interval_sec;

    return FILE_OK;
}

/**
 * @brief 记录一条数据到CSV文件
 * @param record 数据记录指针
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 根据记录的日期自动创建对应文件并写入
 */
uint8_t data_record(const DataRecord* record)
{
    // 检查是否已初始化，若未初始化则返回错误
    if (!g_recorder.initialized || !g_recorder.config.enable) {
        return FILE_ERROR;
    }
    if (write_day_record(record->year, record->month, record->day, record) != FILE_OK) {
        return FILE_ERROR;
    }

    g_recorder.record_count++;

    return FILE_OK;
}

/**
 * @brief 记录流量数据(简化接口)
 * @param water_level 水位(m)
 * @param instant_flow 瞬时流量(m³/s)
 * @param total_flow 累计流量(m³)
 * @param total_time 累计时间(秒)
 * @param temperature 温度(°C)
 * @param flags 标志位
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 自动填充当前时间并调用data_record()
 */
uint8_t data_record_flow(float water_level, float instant_flow,
                         double total_flow, uint32_t total_time,
                         float temperature, uint16_t flags)
{
    DataRecord record;
    RTC_TimeData timeData;

    /* 从RTC获取当前时间 */
    RTC_Time_Get(&timeData);
    record.year = timeData.year;
    record.month = timeData.month;
    record.day = timeData.date;
    record.hour = timeData.hour;
    record.minute = timeData.minute;
    record.second = timeData.second;

    record.water_level = water_level;
    record.instant_flow = instant_flow;
    record.total_flow = total_flow;
    record.total_time = total_time;
    record.temperature = temperature;
    record.flags = flags;

    return data_record(&record);
}

/**
 * @brief 查询指定日期文件的数据
 * @param year 年
 * @param month 月
 * @param day 日
 * @param filter 查询条件(可为NULL表示全部)
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 返回的记录数
 */
static uint32_t query_day_file(uint16_t year, uint8_t month, uint8_t day,
                                const DataQueryFilter* filter,
                                DataQueryCallback callback,
                                void* user_data)
{
    char filepath[64];
    char line_buf[RECORD_LINE_MAX_LEN];
    char chunk[64];
    DataRecord record;
    uint32_t count = 0;
    uint32_t bytes_read;
    uint16_t line_pos = 0;
    uint8_t result;

    make_day_path(year, month, day, filepath, sizeof(filepath));

    if (!file_exists(filepath)) {
        return 0;
    }

    /* 打开文件 */
    result = file_open(filepath, FILE_MODE_READ);
    if (result != FILE_OK) {
        return 0;
    }

    /* 跳过CSV头 */
    file_read(chunk, strlen(g_csv_header), &bytes_read);

    /* 逐行读取: 用小缓冲区逐块读入，按换行符拼装完整行 */
    while (1) {
        result = file_read(chunk, sizeof(chunk), &bytes_read);
        if (result != FILE_OK || bytes_read == 0) {
            break;
        }

        for (uint32_t i = 0; i < bytes_read; i++) {
            if (chunk[i] == '\n') {
                line_buf[line_pos] = '\0';
                /* 去除尾部 \r */
                if (line_pos > 0 && line_buf[line_pos - 1] == '\r') {
                    line_buf[line_pos - 1] = '\0';
                }

                /* 解析记录 */
                if (data_parse_csv_line(line_buf, &record) == FILE_OK) {
                    uint8_t pass = 1;

                    if (filter) {
                        uint32_t record_time = record.hour * 100 + record.minute;
                        uint32_t start_time = filter->start_hour * 100 + filter->start_min;
                        uint32_t end_time = filter->end_hour * 100 + filter->end_min;

                        if (record_time < start_time || record_time > end_time) {
                            pass = 0;
                        }

                        if (filter->filter_alarm_only &&
                            !(record.flags & filter->flags_mask)) {
                            pass = 0;
                        }
                    }

                    if (pass) {
                        if (callback) {
                            callback(&record, user_data);
                        }
                        count++;
                    }
                }
                line_pos = 0;
            } else if (line_pos < sizeof(line_buf) - 1) {
                line_buf[line_pos++] = chunk[i];
            }
        }
    }

    file_close();
    return count;
}

/**
 * @brief 查询历史数据
 * @param filter 查询条件(可为NULL表示全部)
 * @param callback 回调函数，每条记录调用一次
 * @param user_data 用户数据，传递给回调函数
 * @return 返回的记录数
 * @note 遍历日期范围内的所有文件并查询
 */
uint32_t data_query(const DataQueryFilter* filter,
                    DataQueryCallback callback,
                    void* user_data)
{
    uint32_t count = 0;
    uint16_t year, month, day;
    char filepath[64];

    if (!g_recorder.initialized) {
        return 0;
    }

    /* 如果没有指定日期范围，查询所有数据 */
    if (!filter || (filter->start_year == 0 && filter->end_year == 0)) {
        uint16_t start_year = 2024;
        uint16_t end_year = 2030;
        /* 获取当前年份作为结束年份 */
        RTC_TimeData timeData;
        RTC_Time_Get(&timeData);
        end_year = timeData.year;
        /* 遍历所有存在的日期文件 */
        for (year = start_year; year <= end_year; year++) {
            snprintf(filepath, sizeof(filepath), DATA_YEAR_DIR, year);
            if (!file_exists(filepath)) {
                if (year == 2024) continue;
                break;
            }

            for (month = 1; month <= 12; month++) {
                snprintf(filepath, sizeof(filepath), DATA_MONTH_DIR, year, month);
                if (!file_exists(filepath)) {
                    break;
                }

                for (day = 1; day <= 31; day++) {
                    make_day_path(year, month, day, filepath, sizeof(filepath));
                    if (!file_exists(filepath)) {
                        continue;
                    }
                    count += query_day_file(year, month, day, filter, callback, user_data);
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
                    count += query_day_file(year, month, day, filter, callback, user_data);
                }
            }
        }
    }

    return count;
}

/**
 * @brief 按日期查询数据
 * @param year 年
 * @param month 月(0表示全年)
 * @param day 日(0表示整月)
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 返回的记录数
 * @note 内部构建时间范围过滤器并调用data_query()
 */
uint32_t data_query_by_date(uint16_t year, uint8_t month, uint8_t day,
                            DataQueryCallback callback,
                            void* user_data)
{
    DataQueryFilter filter = {0};

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

    return data_query(&filter, callback, user_data);
}

/**
 * @brief 获取数据统计信息
 * @param filter 查询条件(可为NULL表示全部)
 * @param stats 统计结果输出
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 遍历符合条件的所有记录计算统计值
 */
uint8_t data_get_statistics(const DataQueryFilter* filter,
                            DataStatistics* stats)
{
    if (!stats) {
        return FILE_ERROR;
    }

    memset(stats, 0, sizeof(DataStatistics));

    /* 使用回调统计 */
    /* 简化实现：实际应遍历所有记录计算 */

    return FILE_OK;
}

/**
 * @brief 导出数据到指定路径
 * @param filter 查询条件
 * @param output_path 输出文件路径
 * @param format 导出格式(0=CSV 1=JSON)
 * @return 导出的记录数
 */
uint32_t data_export(const DataQueryFilter* filter,
                     const char* output_path,
                     uint8_t format)
{
    /* 简化实现：复制查询结果到新文件 */
    (void)filter;
    (void)output_path;
    (void)format;
    return 0;
}

/**
 * @brief 导出数据到串口
 * @param filter 查询条件(可为NULL表示全部)
 * @return 导出的记录数
 * @note 通过printf输出到串口
 */
uint32_t data_export_to_console(const DataQueryFilter* filter)
{
    uint32_t count = data_query(filter, NULL, NULL);

    printf("=== Data Export ===\r\n");
    printf("Total records: %lu\r\n", count);

    return count;
}

/**
 * @brief 删除指定日期之前的数据
 * @param year 年
 * @param month 月
 * @param day 日
 * @return 删除的记录数
 */
uint32_t data_delete_before(uint16_t year, uint8_t month, uint8_t day)
{
    uint32_t delete_count = 0;
    char filepath[64];
    uint16_t y;
    uint8_t m, d;

    if (!g_recorder.initialized) {
        return 0;
    }

    /* 遍历所有文件，删除指定日期之前的 */
    for (y = 2024; y <= 2030; y++) {
        snprintf(filepath, sizeof(filepath), DATA_YEAR_DIR, y);
        if (!file_exists(filepath)) {
            if (y == 2024) continue;
            break;
        }

        for (m = 1; m <= 12; m++) {
            /* 如果是目标年份和月份，检查日期 */
            if (y == year && m > month) {
                break;
            }
            if (y == year && m == month) {
                for (d = 1; d < day; d++) {
                    make_day_path(y, m, d, filepath, sizeof(filepath));
                    if (file_exists(filepath)) {
                        file_delete(filepath);
                        delete_count++;
                    }
                }
            } else if (y < year || (y == year && m < month)) {
                /* 删除整月目录 */
                snprintf(filepath, sizeof(filepath), DATA_MONTH_DIR, y, m);
                if (file_exists(filepath)) {
                    /* 删除该月所有天 */
                    for (d = 1; d <= 31; d++) {
                        make_day_path(y, m, d, filepath, sizeof(filepath));
                        if (file_exists(filepath)) {
                            file_delete(filepath);
                            delete_count++;
                        }
                    }
                }
            }
        }

        /* 如果该年所有月份都删除了，删除年份目录 */
        if (y < year) {
            snprintf(filepath, sizeof(filepath), DATA_YEAR_DIR, y);
            if (file_exists(filepath)) {
                /* 检查是否为空 */
            }
        }
    }

    return delete_count;
}

/**
 * @brief 清理过期数据
 * @param keep_days 保留最近N天的数据
 * @return 删除的文件数
 * @note 内部调用data_delete_before()删除过期数据
 */
uint32_t data_cleanup(uint16_t keep_days)
{
    if (keep_days > DATA_MAX_RETENTION_DAYS) {
        keep_days = DATA_MAX_RETENTION_DAYS;
    }

    /* 计算截止日期 */
    RTC_TimeData timeData;
    RTC_Time_Get(&timeData);

    /* 防止日期无符号下溢: keep_days > date 时安全处理 */
    if (keep_days >= timeData.date) {
        /* 需要回溯到上个月, 简化处理: 保留当月所有数据 */
        return 0;
    }

    return data_delete_before(timeData.year, timeData.month, timeData.date - keep_days);
}

/**
 * @brief 按日期清理数据
 * @param year 年(0表示当前年)
 * @param month 月(0表示当前月)
 * @param day 日(0表示删除该月所有之前的数据)
 * @return 删除的文件数
 */
uint32_t data_cleanup_by_date(uint16_t year, uint8_t month, uint8_t day)
{
    /* 如果传入0，从RTC获取当前时间 */
    if (year == 0 || month == 0) {
        RTC_TimeData timeData;
        RTC_Time_Get(&timeData);
        if (year == 0) year = timeData.year;
        if (month == 0) month = timeData.month;
        if (day == 0) day = timeData.date;
    }
    if (day == 0) day = 1;

    return data_delete_before(year, month, day);
}

/**
 * @brief 获取已记录的总数
 * @return 记录总数
 */
uint32_t data_get_record_count(void)
{
    return g_recorder.record_count;
}

/**
 * @brief 获取数据文件大小
 * @return 文件大小(字节)
 */
uint32_t data_get_file_size(void)
{
    return g_recorder.file_size;
}

/**
 * @brief 设置数据记录间隔
 * @param interval_sec 间隔秒数(1-3600)
 */
void data_set_interval(uint16_t interval_sec)
{
    if (interval_sec >= 1 && interval_sec <= 3600) {
        g_recorder.config.interval_sec = interval_sec;
    }
}

/**
 * @brief 启用/禁用数据记录
 * @param enable 1:启用 0:禁用
 */
void data_set_enable(uint8_t enable)
{
    g_recorder.config.enable = enable ? 1 : 0;
}

/**
 * @brief 强制刷新数据到SD卡
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 由于写入时已调用f_sync，此函数直接返回成功
 */
uint8_t data_flush(void)
{
    return FILE_OK;
}

/**
 * @brief 格式化数据文件
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 删除整个/data目录并重新创建
 */
uint8_t data_format(void)
{
    char filepath[64];
    uint16_t y;
    uint8_t m, d;

    if (!g_recorder.initialized) {
        return FILE_ERROR;
    }

    /* 获取当前年份 */
    RTC_TimeData timeData;
    RTC_Time_Get(&timeData);

    /* 删除所有数据文件 */
    for (y = 2024; y <= timeData.year; y++) {
        for (m = 1; m <= 12; m++) {
            for (d = 1; d <= 31; d++) {
                make_day_path(y, m, d, filepath, sizeof(filepath));
                if (file_exists(filepath)) {
                    file_delete(filepath);
                }
            }
        }

        snprintf(filepath, sizeof(filepath), DATA_YEAR_DIR, y);
        if (file_exists(filepath)) {
            file_delete(filepath);
        }
    }

    g_recorder.record_count = 0;
    g_recorder.file_size = 0;

    /* 重建根目录 */
    if (!file_exists(DATA_BASE_PATH)) {
        file_create_dir(DATA_BASE_PATH);
    }

    return FILE_OK;
}

/**
 * @brief 关闭数据记录器
 * @note 刷新数据并清除初始化标志
 */
void data_recorder_deinit(void)
{
    if (!g_recorder.initialized) {
        return;
    }

    data_flush();
    g_recorder.initialized = 0;
}

/**
 * @brief 解析CSV行为数据记录
 * @param line CSV行字符串
 * @param record 输出记录结构
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 解析格式: YYYY-MM-DD HH:MM:SS,water_level,instant_flow,total_flow,temperature,flags
 */
/**
 * @brief 跳到下一个字段 (跳过当前字段到逗号或行尾)
 */
static const char* skip_to_comma(const char *p)
{
    while (*p && *p != ',' && *p != '\r' && *p != '\n') p++;
    if (*p == ',') p++;
    return p;
}

/**
 * @brief 手动解析浮点数
 */
static float parse_float(const char *s, const char **next)
{
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    double integer = 0.0;
    while (*s >= '0' && *s <= '9') {
        integer = integer * 10.0 + (*s - '0');
        s++;
    }

    float frac = 0.0f;
    if (*s == '.') {
        s++;
        float div = 10.0f;
        while (*s >= '0' && *s <= '9') {
            frac += (float)(*s - '0') / div;
            div *= 10.0f;
            s++;
        }
    }

    if (next) *next = s;
    float result = (float)integer + frac;
    return neg ? -result : result;
}

/**
 * @brief 手动解析双精度浮点数
 */
static double parse_double(const char *s, const char **next)
{
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    double integer = 0.0;
    while (*s >= '0' && *s <= '9') {
        integer = integer * 10.0 + (*s - '0');
        s++;
    }

    double frac = 0.0;
    if (*s == '.') {
        s++;
        double div = 10.0;
        while (*s >= '0' && *s <= '9') {
            frac += (*s - '0') / div;
            div *= 10.0;
            s++;
        }
    }

    if (next) *next = s;
    double result = (double)integer + frac;
    return neg ? -result : result;
}

uint8_t data_parse_csv_line(const char* line, DataRecord* record)
{
    if (!line || !record) {
        return FILE_ERROR;
    }

    const char *p = line;

    /* 解析日期时间: YYYY-MM-DD HH:MM:SS */
    record->year   = (uint16_t)atoi(p);  p = skip_to_comma(skip_to_comma(skip_to_comma(skip_to_comma(skip_to_comma(skip_to_comma(p)))))); /* skip to first , */

    /* 重新解析，逐字段跳过分隔符 */
    p = line;
    record->year   = (uint16_t)atoi(p);  while (*p && *p != '-') p++; if (!*p) return FILE_ERROR; p++;
    record->month  = (uint8_t)atoi(p);   while (*p && *p != '-') p++; if (!*p) return FILE_ERROR; p++;
    record->day    = (uint8_t)atoi(p);   while (*p && *p != ' ') p++; if (!*p) return FILE_ERROR; p++;
    record->hour   = (uint8_t)atoi(p);   while (*p && *p != ':') p++; if (!*p) return FILE_ERROR; p++;
    record->minute = (uint8_t)atoi(p);   while (*p && *p != ':') p++; if (!*p) return FILE_ERROR; p++;
    record->second = (uint8_t)atoi(p);   while (*p && *p != ',') p++; if (!*p) return FILE_ERROR; p++;

    /* water_level */
    record->water_level = parse_float(p, &p); while (*p && *p != ',') p++; if (!*p) return FILE_ERROR; p++;

    /* instant_flow */
    record->instant_flow = parse_float(p, &p); while (*p && *p != ',') p++; if (!*p) return FILE_ERROR; p++;

    /* total_flow */
    record->total_flow = parse_double(p, &p);
    while (*p && *p != ',') p++; if (!*p) return FILE_ERROR; p++;

    /* total_time */
    record->total_time = (uint32_t)atol(p); while (*p && *p != ',') p++; if (!*p) return FILE_ERROR; p++;

    /* temperature */
    record->temperature = parse_float(p, &p); while (*p && *p != ',') p++;
    p++; /* skip comma or end */

    /* flags */
    record->flags = (uint16_t)atoi(p);

    return FILE_OK;
}

/**
 * @brief 将数据记录格式化为CSV行
 * @param record 记录结构
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 实际写入的字符数
 */
uint32_t data_format_csv_line(const DataRecord* record,
                              char* buffer,
                              uint32_t size)
{
    if (!record || !buffer) {
        return 0;
    }

    return snprintf(buffer, size,
                    "%04u-%02u-%02u %02u:%02u:%02u,%.3f,%.6f,%.6f,%lu,%.1f,%u\r\n",
                    record->year, record->month, record->day,
                    record->hour, record->minute, record->second,
                    record->water_level,
                    record->instant_flow,
                    record->total_flow,
                    record->total_time,
                    record->temperature,
                    record->flags);
}
