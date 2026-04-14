/**
 * @file rtc_time.c
 * @brief RTC时间管理模块实现
 * @details 提供统一的时间读取、设置和格式化功能
 * @note 封装STM32 HAL的RTC外设
 */

#include "rtc_time.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief 全局RTC时间数据
 * @note 各模块直接访问，减少频繁的RTC硬件读取
 */
RTC_TimeData g_RtcTime = {0};

/**
 * @brief 每月天数查找表(辅助表)
 * @note 用于计算Unix时间戳
 */
static const uint8_t g_daysOfMonth[] = {
    0,  /* 占位 */
    31, /* 1月 */
    28, /* 2月 */
    31, /* 3月 */
    30, /* 4月 */
    31, /* 5月 */
    30, /* 6月 */
    31, /* 7月 */
    31, /* 8月 */
    30, /* 9月 */
    31, /* 10月 */
    30, /* 11月 */
    31  /* 12月 */
};

/**
 * @brief RTC时间模块初始化函数
 * @note 由外部调用，目前为空实现
 *       实际初始化由MX_RTC_Init()完成
 * @return 无
 */
void RTC_Time_Init(void)
{
}

/**
 * @brief 获取当前RTC时间
 * @note 从RTC硬件读取当前时间到用户结构体
 *       先读时间后读日期，保证数据一致性
 * @param[out] timeData 时间数据输出缓冲指针
 * @return 无
 * @note 必须传入有效指针
 */
void RTC_Time_Get(RTC_TimeData* timeData)
{
    RTC_DateTypeDef sDate = {0};
    RTC_TimeTypeDef sTime = {0};

    if (timeData == NULL) {
        return;
    }

    /* 先读时间，后读日期 - 遵循HAL推荐的方式 */
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    /* 转换为用户格式 (RTC存储年份为BCD码，2000年基准) */
    timeData->year = 2000 + sDate.Year;   /* RTC只存储后两位年，需要加2000 */
    timeData->month = sDate.Month;
    timeData->date = sDate.Date;
    timeData->hour = sTime.Hours;
    timeData->minute = sTime.Minutes;
    timeData->second = sTime.Seconds;
    timeData->weekDay = sDate.WeekDay;
}

/**
 * @brief 设置RTC时间
 * @note 将用户时间结构体数据写入RTC硬件
 *       先设时间，后设日期
 * @param[in] timeData 时间数据输入缓冲区指针
 * @return 无
 * @note 必须传入有效指针
 * @warning 此操作会永久有效
 */
void RTC_Time_Set(const RTC_TimeData* timeData)
{
    RTC_DateTypeDef sDate = {0};
    RTC_TimeTypeDef sTime = {0};

    if (timeData == NULL) {
        return;
    }

    /* 设置时间 */
    sTime.Hours = timeData->hour;
    sTime.Minutes = timeData->minute;
    sTime.Seconds = timeData->second;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;    /* 不使用夏令时 */
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;  /* 清除夏令时标志 */

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

    /* 设置日期 */
    sDate.Year = (uint8_t)(timeData->year % 100);   /* RTC只存储年份后两位 */
    sDate.Month = timeData->month;
    sDate.Date = timeData->date;
    sDate.WeekDay = timeData->weekDay;

    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

/**
 * @brief 设置RTC时间（直接传参数值版）
 * @note 将各时间参数值直接写入RTC硬件
 * @param[in] year 年份，如2026
 * @param[in] month 月份，1-12
 * @param[in] date 日期，1-31
 * @param[in] hour 小时，0-23
 * @param[in] minute 分钟，0-59
 * @param[in] second 秒钟，0-59
 * @param[in] weekDay 星期，1-7 (Sunday=1, Saturday=7)
 * @return 无
 * @warning 此操作会永久有效，RTC立即生效
 */
void RTC_Time_SetValues(uint16_t year, uint8_t month, uint8_t date,
                        uint8_t hour, uint8_t minute, uint8_t second,
                        uint8_t weekDay)
{
    RTC_TimeData timeData;

    timeData.year = year;
    timeData.month = month;
    timeData.date = date;
    timeData.hour = hour;
    timeData.minute = minute;
    timeData.second = second;
    timeData.weekDay = weekDay;

    RTC_Time_Set(&timeData);
}

/**
 * @brief 获取指定格式的时间字符串
 * @note 根据format参数返回不同格式的时间字符串
 * @param[out] buffer 字符串输出缓冲区，至少32字节
 * @param[in] format 时间格式枚举
 * @return 无
 */
void RTC_Time_GetString(char* buffer, RTC_TimeFormat format)
{
    RTC_TimeData timeData;

    if (buffer == NULL) {
        return;
    }

    /* 获取当前时间数据 */
    RTC_Time_Get(&timeData);

    /* 根据格式生成字符串 */
    switch (format) {
        case RTC_TIME_FORMAT_DATE:
            /* 日期格式: YYYY-MM-DD */
            snprintf(buffer, RTC_TIME_STRING_LEN, "%04d-%02d-%02d",
                    timeData.year, timeData.month, timeData.date);
            break;

        case RTC_TIME_FORMAT_TIME:
            /* 时间格式: HH:MM:SS */
            snprintf(buffer, RTC_TIME_STRING_LEN, "%02d:%02d:%02d",
                    timeData.hour, timeData.minute, timeData.second);
            break;

        case RTC_TIME_FORMAT_FULL:
            /* 完整格式: YYYY-MM-DD HH:MM:SS */
            snprintf(buffer, RTC_TIME_STRING_LEN, "%04d-%02d-%02d %02d:%02d:%02d",
                    timeData.year, timeData.month, timeData.date,
                    timeData.hour, timeData.minute, timeData.second);
            break;

        case RTC_TIME_FORMAT_COMPACT:
            /* 紧凑格式: YYYYMMDDHHMMSS */
            snprintf(buffer, RTC_TIME_STRING_LEN, "%04d%02d%02d%02d%02d%02d",
                    timeData.year, timeData.month, timeData.date,
                    timeData.hour, timeData.minute, timeData.second);
            break;

        case RTC_TIME_FORMAT_LOG_FILE:
            /* 日志文件路径格式: YYYY/MM/DD */
            snprintf(buffer, RTC_TIME_STRING_LEN, "%04d/%02d/%02d",
                    timeData.year, timeData.month, timeData.date);
            break;

        case RTC_TIME_FORMAT_LOG_DATETIME:
            /* 日志时间格式: YYYY-MM-DD HH:MM:SS */
            snprintf(buffer, RTC_TIME_STRING_LEN, "%04d-%02d-%02d %02d:%02d:%02d",
                    timeData.year, timeData.month, timeData.date,
                    timeData.hour, timeData.minute, timeData.second);
            break;

        default:
            buffer[0] = '\0';
            break;
    }
}

/**
 * @brief 获取Unix时间戳
 * @note 计算从1970-01-01 00:00:00到当前的秒数
 *       不考虑闰秒，简化月份处理
 * @return Unix时间戳(秒)
 * @note 未处理时区，默认返回UTC时间
 */
uint32_t RTC_Time_GetTimestamp(void)
{
    RTC_TimeData timeData;
    uint32_t timestamp = 0;
    uint16_t y;
    uint8_t m;

    /* 获取当前时间 */
    RTC_Time_Get(&timeData);

    y = timeData.year;
    m = timeData.month;

    /* 累加从1970年到指定年份的天数 */
    for (uint16_t i = 1970; i < y; i++) {
        /* 闰年判断: 能被4整除且不能被100整除，或者能被400整除 */
        if ((i % 4 == 0 && i % 100 != 0) || (i % 400 == 0)) {
            timestamp += 366 * 24 * 3600;  /* 闰年366天 */
        } else {
            timestamp += 365 * 24 * 3600;  /* 平年365天 */
        }
    }

    /* 累加从年初到指定月份的天数 */
    for (uint8_t i = 1; i < m; i++) {
        if (i == 2) {
            /* 2月份需要判断闰年 */
            if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
                timestamp += 29 * 24 * 3600;  /* 闰年2月29天 */
            } else {
                timestamp += 28 * 24 * 3600;  /* 平年2月28天 */
            }
        } else {
            timestamp += g_daysOfMonth[i] * 24 * 3600;
        }
    }

    /* 计算当月的天数 (日期从1开始，所以减1) */
    timestamp += (timeData.date - 1) * 24 * 3600;

    /* 加上时分秒 */
    timestamp += timeData.hour * 3600;
    timestamp += timeData.minute * 60;
    timestamp += timeData.second;

    return timestamp;
}
