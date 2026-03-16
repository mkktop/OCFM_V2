/**
 * @file rtc_time.h
 * @brief RTC时间管理模块 - 统一接口
 * @details 为日志系统和屏幕显示提供统一的时间获取和格式化功能
 * @note 本模块封装了STM32 HAL层的RTC操作，提供更简洁的上层API
 * @note 使用前请确保RTC已经初始化(MX_RTC_Init已调用)
 */

#ifndef __RTC_TIME_H
#define __RTC_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "rtc.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 模块版本号
 */
#define RTC_TIME_VERSION    "1.0.0"

/**
 * @brief 时间字符串缓冲区最小长度
 */
#define RTC_TIME_STRING_LEN 32

/**
 * @brief 时间数据结构体
 * @note 用于存储年、月、日、时、分、秒、星期信息
 */
typedef struct {
    uint16_t year;      /* 年份，如2026 */
    uint8_t month;      /* 月份，1-12 */
    uint8_t date;       /* 日期，1-31 */
    uint8_t hour;       /* 小时，0-23 */
    uint8_t minute;     /* 分钟，0-59 */
    uint8_t second;     /* 秒钟，0-59 */
    uint8_t weekDay;    /* 星期，1-7 (Sunday=1, Saturday=7) */
} RTC_TimeData;

/**
 * @brief 时间格式枚举
 * @note 用于指定不同用途的时间字符串格式
 */
typedef enum {
    RTC_TIME_FORMAT_NONE = 0,
    RTC_TIME_FORMAT_DATE,
    RTC_TIME_FORMAT_TIME,
    RTC_TIME_FORMAT_FULL,
    RTC_TIME_FORMAT_COMPACT,
    RTC_TIME_FORMAT_LOG_FILE,
    RTC_TIME_FORMAT_LOG_DATETIME,
} RTC_TimeFormat;

/**
 * @brief 星期几枚举
 */
typedef enum {
    RTC_TIME_WEEKDAY_SUNDAY = 1,
    RTC_TIME_WEEKDAY_MONDAY = 2,
    RTC_TIME_WEEKDAY_TUESDAY = 3,
    RTC_TIME_WEEKDAY_WEDNESDAY = 4,
    RTC_TIME_WEEKDAY_THURSDAY = 5,
    RTC_TIME_WEEKDAY_FRIDAY = 6,
    RTC_TIME_WEEKDAY_SATURDAY = 7,
} RTC_WeekDay;


/**
 * @brief RTC时间模块初始化
 * @note 在main函数中MX_RTC_Init()之后调用
 * @return 无
 */
void RTC_Time_Init(void);

/**
 * @brief 检查RTC时间是否已设置
 * @note 用于判断RTC是否处于默认状态(2026-01-01 00:00:00)
 * @return true - 时间已设置 valid, false - 时间未设置或处于默认状态
 */
bool RTC_Time_IsValid(void);

/**
 * @brief 获取当前RTC时间
 * @note 从RTC硬件读取当前时间到结构体
 * @param[out] timeData 时间数据输出缓冲区指针
 * @return 无
 * @note 必须传入有效指针，否则可能导致HardFault
 */
void RTC_Time_Get(RTC_TimeData* timeData);

/**
 * @brief 设置RTC时间
 * @note 将时间数据写入RTC硬件
 * @param[in] timeData 时间数据输入缓冲区指针
 * @return 无
 * @note 必须传入有效指针，否则可能导致HardFault
 * @warning 设置时间后会立即生效，RTC继续运行
 */
void RTC_Time_Set(const RTC_TimeData* timeData);

/**
 * @brief 设置RTC时间（直接传入数值）
 * @note 将年月日时分秒等数值写入RTC硬件
 * @param[in] year 年份，如2026
 * @param[in] month 月份，1-12
 * @param[in] date 日期，1-31
 * @param[in] hour 小时，0-23
 * @param[in] minute 分钟，0-59
 * @param[in] second 秒钟，0-59
 * @param[in] weekDay 星期，1-7 (Sunday=1, Saturday=7)
 * @return 无
 * @warning 设置后会立即生效，RTC继续运行
 */
void RTC_Time_SetValues(uint16_t year, uint8_t month, uint8_t date,
                        uint8_t hour, uint8_t minute, uint8_t second,
                        uint8_t weekDay);

/**
 * @brief 获取格式化的时间字符串
 * @note 根据指定的格式获取时间字符串
 * @param[out] buffer 字符串输出缓冲区，需要至少32字节空间
 * @param[in] format 时间格式枚举
 * @return 无
 * @note 缓冲区最小需要32字节，否则可能导致缓冲区溢出
 */
void RTC_Time_GetString(char* buffer, RTC_TimeFormat format);

/**
 * @brief 获取日期字符串
 * @note 格式: YYYY-MM-DD (如 2026-03-16)
 * @param[out] buffer 字符串输出缓冲区，需要至少32字节空间
 * @return 无
 */
void RTC_Time_GetDateString(char* buffer);

/**
 * @brief 获取时间字符串
 * @note 格式: HH:MM:SS (如 12:30:45)
 * @param[out] buffer 字符串输出缓冲区，需要至少32字节空间
 * @return 无
 */
void RTC_Time_GetTimeString(char* buffer);

/**
 * @brief 获取完整的日期时间字符串
 * @note 格式: YYYY-MM-DD HH:MM:SS (如 2026-03-16 12:30:45)
 * @param[out] buffer 字符串输出缓冲区，需要至少32字节空间
 * @return 无
 */
void RTC_Time_GetFullString(char* buffer);

/**
 * @brief 获取Unix时间戳
 * @note 返回自1970-01-01 00:00:00以来的秒数
 * @return Unix时间戳(秒)
 * @note 此函数未考虑时区，默认返回UTC时间戳
 */
uint32_t RTC_Time_GetTimestamp(void);

/**
 * @brief 获取星期几的英文名称
 * @param[in] weekDay 星期几(1-7，对应RTC_WeekDay枚举)
 * @return 星期几的英文名称字符串
 */
const char* RTC_Time_GetWeekDayString(uint8_t weekDay);

/**
 * @brief 获取月份的英文名称
 * @param[in] month 月份(1-12)
 * @return 月份的英文名称字符串
 */
const char* RTC_Time_GetMonthString(uint8_t month);

/**
 * @brief 手动更新时间
 * @note 供外部定时任务调用，更新内部缓存(如有)
 * @return 无
 */
void RTC_Time_Update(void);

/**
 * @brief 全局RTC时间数据变量
 * @note 在rtc_time.c中定义，其他文件通过extern引用
 */
extern RTC_TimeData g_RtcTime;

#ifdef __cplusplus
}
#endif

#endif /* __RTC_TIME_H */
