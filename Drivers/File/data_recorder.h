/**
 * @file data_recorder.h
 * @brief 数据记录器头文件
 * @details 提供流量历史数据的CSV格式记录和查询功能
 *          支持按日期分文件存储、按时间查询、数据导出、自动存储管理
 * @note 存储方式: /data/YYYY/MM/DD.csv (每天一个文件)
 */

#ifndef __DATA_RECORDER_H
#define __DATA_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "file_driver.h"

/**
 * @brief 数据记录器版本
 */
#define DATA_RECORDER_VERSION   0x0100  /* v1.0 */

/**
 * @brief 单条记录最大长度
 */
#define RECORD_LINE_MAX_LEN     256

/**
 * @brief CSV文件路径
 * @note 存储结构: /data/YYYY/MM/DD.csv (按年/月/日分目录)
 */
#define DATA_BASE_PATH         "/data"
#define DATA_YEAR_DIR          "/data/%04u"
#define DATA_MONTH_DIR         "/data/%04u/%02u"
#define DATA_DAY_FILE          "/data/%04u/%02u/%02u.csv"

/**
 * @brief 旧版本兼容定义
 */
#define DATA_CSV_PATH          DATA_DAY_FILE
#define DATA_CSV_TEMP_PATH     "/data/temp.csv"

/**
 * @brief 默认数据存储间隔(秒)
 */
#define DATA_DEFAULT_INTERVAL   300     /* 5分钟 */

/**
 * @brief 最大数据保留天数
 */
#define DATA_MAX_RETENTION_DAYS 365

/**
 * @brief 单次查询最大返回记录数
 */
#define DATA_QUERY_MAX_RECORDS  1000

/**
 * @brief 数据记录结构体
 */
typedef struct {
    uint16_t year;          /**< 年 */
    uint8_t  month;         /**< 月 */
    uint8_t  day;           /**< 日 */
    uint8_t  hour;          /**< 时 */
    uint8_t  minute;        /**< 分 */
    uint8_t  second;        /**< 秒 */
    float    water_level;   /**< 水位(m) */
    float    instant_flow;  /**< 瞬时流量(m?/s) */
    double   total_flow;    /**< 累计流量(m?) */
    float    temperature;   /**< 温度(°C) */
    uint16_t flags;         /**< 标志位(0x0001=高水位报警, 0x0002=低水位报警, ...) */
    uint32_t total_time;    /**< 累计时间(秒)，累计流量记录时间 */
} DataRecord;

/**
 * @brief 数据记录器配置结构体
 */
typedef struct {
    uint8_t  enable;                /**< 是否启用记录 */
    uint16_t interval_sec;          /**< 记录间隔(秒) */
    uint16_t retention_days;        /**< 数据保留天数 */
    uint8_t  csv_header;            /**< 是否写入CSV头行 */
    uint32_t max_file_size;         /**< 文件最大大小(字节) */
} DataRecorderConfig;

/**
 * @brief 查询条件结构体
 */
typedef struct {
    uint16_t start_year;            /**< 起始年 */
    uint8_t  start_month;           /**< 起始月 */
    uint8_t  start_day;             /**< 起始日 */
    uint8_t  start_hour;            /**< 起始时 */
    uint8_t  start_min;             /**< 起始分 */

    uint16_t end_year;              /**< 结束年 */
    uint8_t  end_month;             /**< 结束月 */
    uint8_t  end_day;               /**< 结束日 */
    uint8_t  end_hour;              /**< 结束时 */
    uint8_t  end_min;               /**< 结束分 */

    uint8_t  filter_alarm_only;     /**< 仅查询报警记录 */
    uint16_t flags_mask;            /**< 标志位掩码 */
} DataQueryFilter;

/**
 * @brief 查询结果回调函数类型
 */
typedef void (*DataQueryCallback)(const DataRecord* record, void* user_data);

/**
 * @brief 数据统计结构体
 */
typedef struct {
    uint32_t record_count;          /**< 记录总数 */
    float    avg_water_level;       /**< 平均水位 */
    float    max_water_level;       /**< 最高水位 */
    float    min_water_level;       /**< 最低水位 */
    float    avg_flow;              /**< 平均流量 */
    float    max_flow;              /**< 最大流量 */
    double   total_flow_delta;      /**< 总流量变化量 */
    uint32_t alarm_count;           /**< 报警次数 */
} DataStatistics;

/**
 * @brief 数据记录器初始化
 * @param config 配置指针，传NULL使用默认配置
 * @return 0:成功 1:失败
 */
uint8_t data_recorder_init(const DataRecorderConfig* config);

/**
 * @brief 记录一条数据
 * @param record 数据记录指针
 * @return 0:成功 1:失败
 */
uint8_t data_record(const DataRecord* record);

/**
 * @brief 记录当前流量数据(简化接口)
 * @param water_level 水位(m)
 * @param instant_flow 瞬时流量(m?/s)
 * @param total_flow 累计流量(m?)
 * @param total_time 累计时间(秒)
 * @param temperature 温度(°C)
 * @param flags 标志位
 * @return 0:成功 1:失败
 */
uint8_t data_record_flow(float water_level, float instant_flow,
                         double total_flow, uint32_t total_time,
                         float temperature, uint16_t flags);

/**
 * @brief 查询历史数据
 * @param filter 查询条件
 * @param callback 回调函数，每条记录调用一次
 * @param user_data 用户数据，传递给回调函数
 * @return 返回的记录数
 */
uint32_t data_query(const DataQueryFilter* filter,
                    DataQueryCallback callback,
                    void* user_data);

/**
 * @brief 按日期查询数据
 * @param year 年
 * @param month 月(0表示全年)
 * @param day 日(0表示整月)
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 返回的记录数
 */
uint32_t data_query_by_date(uint16_t year, uint8_t month, uint8_t day,
                            DataQueryCallback callback,
                            void* user_data);

/**
 * @brief 获取数据统计信息
 * @param filter 查询条件
 * @param stats 统计结果输出
 * @return 0:成功 1:失败
 */
uint8_t data_get_statistics(const DataQueryFilter* filter,
                            DataStatistics* stats);

/**
 * @brief 导出数据到指定路径
 * @param filter 查询条件
 * @param output_path 输出文件路径
 * @param format 导出格式(0=CSV 1=JSON)
 * @return 导出的记录数
 */
uint32_t data_export(const DataQueryFilter* filter,
                     const char* output_path,
                     uint8_t format);

/**
 * @brief 导出数据到串口
 * @param filter 查询条件
 * @return 导出的记录数
 */
uint32_t data_export_to_console(const DataQueryFilter* filter);

/**
 * @brief 删除指定日期之前的数据
 * @param year 年
 * @param month 月
 * @param day 日
 * @return 删除的记录数
 */
uint32_t data_delete_before(uint16_t year, uint8_t month, uint8_t day);

/**
 * @brief 清理过期数据
 * @param keep_days 保留天数
 * @return 删除的记录数
 */
uint32_t data_cleanup(uint16_t keep_days);

/**
 * @brief 按日期清理数据
 * @param year 年(0表示当前年)
 * @param month 月(0表示当前月)
 * @param day 日(0表示删除该月所有之前的数据)
 * @return 删除的文件数
 */
uint32_t data_cleanup_by_date(uint16_t year, uint8_t month, uint8_t day);

/**
 * @brief 获取记录总数
 * @return 记录总数
 */
uint32_t data_get_record_count(void);

/**
 * @brief 获取数据文件大小
 * @return 文件大小(字节)
 */
uint32_t data_get_file_size(void);

/**
 * @brief 设置记录间隔
 * @param interval_sec 间隔秒数
 */
void data_set_interval(uint16_t interval_sec);

/**
 * @brief 启用/禁用记录
 * @param enable 1:启用 0:禁用
 */
void data_set_enable(uint8_t enable);

/**
 * @brief 强制刷新数据到SD卡
 * @return 0:成功 1:失败
 */
uint8_t data_flush(void);

/**
 * @brief 格式化数据文件(清空所有历史数据)
 * @return 0:成功 1:失败
 */
uint8_t data_format(void);

/**
 * @brief 关闭数据记录器
 */
void data_recorder_deinit(void);

/**
 * @brief 解析CSV行到记录结构
 * @param line CSV行字符串
 * @param record 输出记录结构
 * @return 0:成功 1:失败
 */
uint8_t data_parse_csv_line(const char* line, DataRecord* record);

/**
 * @brief 将记录格式化为CSV行
 * @param record 记录结构
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 实际写入长度
 */
uint32_t data_format_csv_line(const DataRecord* record,
                              char* buffer,
                              uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* __DATA_RECORDER_H */
