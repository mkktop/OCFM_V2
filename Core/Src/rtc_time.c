/**
 * @file rtc_time.c
 * @brief RTC时间管理模块实现
 * @details 提供统一的时间获取、设置和格式化功能
 * @note 依赖于STM32 HAL层的RTC驱动
 */

#include "rtc_time.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief 全局RTC时间数据
 * @note 供其他模块直接访问，避免频繁调用RTC获取函数
 */
RTC_TimeData g_RtcTime = {0};

/**
 * @brief 星期几英文名称数组
 * @note 索引1-7对应Sunday到Saturday
 */
static const char* const g_weekDayStrings[] = {
    "",           /* 索引0占位 */
    "Sunday",     /* 1 - 星期日 */
    "Monday",     /* 2 - 星期一 */
    "Tuesday",    /* 3 - 星期二 */
    "Wednesday",  /* 4 - 星期三 */
    "Thursday",   /* 5 - 星期四 */
    "Friday",     /* 6 - 星期五 */
    "Saturday"    /* 7 - 星期六 */
};

/**
 * @brief 月份英文名称数组
 * @note 索引1-12对应January到December
 */
static const char* const g_monthStrings[] = {
    "",            /* 索引0占位 */
    "January",     /* 1 - 一月 */
    "February",    /* 2 - 二月 */
    "March",       /* 3 - 三月 */
    "April",       /* 4 - 四月 */
    "May",         /* 5 - 五月 */
    "June",        /* 6 - 六月 */
    "July",        /* 7 - 七月 */
    "August",      /* 8 - 八月 */
    "September",   /* 9 - 九月 */
    "October",     /* 10 - 十月 */
    "November",    /* 11 - 十一月 */
    "December"     /* 12 - 十二月 */
};

/**
 * @brief 各月份天数数组(非闰年)
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
 * @note 供外部调用，目前为空实现
 *       实际初始化由MX_RTC_Init()完成
 * @return 无
 */
void RTC_Time_Init(void)
{
}

/**
 * @brief 检查RTC时间是否有效(已设置)
 * @note 通过判断是否为默认初始值来判断时间是否已设置
 *       默认初始值为: 2026-01-01 00:00:00
 * @return true  - 时间已设置
 * @return false - 时间未设置(处于默认状态)
 */
bool RTC_Time_IsValid(void)
{
    RTC_DateTypeDef sDate = {0};
    RTC_TimeTypeDef sTime = {0};

    /* 先获取时间，再获取日期 */
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    /* 判断是否处于默认状态(2026-01-01 00:00:00) */
    if (sDate.Year == 0 && sDate.Month == 1 && sDate.Date == 1 && 
        sTime.Hours == 0 && sTime.Minutes == 0 && sTime.Seconds == 0) {
        return false;
    }

    return true;
}

/**
 * @brief 获取当前RTC时间
 * @note 从RTC硬件读取当前时间到用户结构体
 *       先读时间再读日期，保证数据一致性
 * @param[out] timeData 时间数据输出缓冲区指针
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

    /* 先读时间，再读日期 - 这是HAL库推荐的方式 */
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    /* 转换为用户格式 (RTC存储的是BCD码和2000基准年) */
    timeData->year = 2000 + sDate.Year;   /* RTC只存两位年份，需要加2000 */
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
 *       先设置时间，再设置日期
 * @param[in] timeData 时间数据输入缓冲区指针
 * @return 无
 * @note 必须传入有效指针
 * @warning 设置后会立即生效
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
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;  /* 重置夏令时标志 */

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

    /* 设置日期 */
    sDate.Year = (uint8_t)(timeData->year % 100);   /* RTC只存储年份后两位 */
    sDate.Month = timeData->month;
    sDate.Date = timeData->date;
    sDate.WeekDay = timeData->weekDay;

    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
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
            sprintf(buffer, "%04d-%02d-%02d", 
                    timeData.year, timeData.month, timeData.date);
            break;

        case RTC_TIME_FORMAT_TIME:
            /* 时间格式: HH:MM:SS */
            sprintf(buffer, "%02d:%02d:%02d", 
                    timeData.hour, timeData.minute, timeData.second);
            break;

        case RTC_TIME_FORMAT_FULL:
            /* 完整格式: YYYY-MM-DD HH:MM:SS */
            sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", 
                    timeData.year, timeData.month, timeData.date,
                    timeData.hour, timeData.minute, timeData.second);
            break;

        case RTC_TIME_FORMAT_COMPACT:
            /* 紧凑格式: YYYYMMDDHHMMSS */
            sprintf(buffer, "%04d%02d%02d%02d%02d%02d", 
                    timeData.year, timeData.month, timeData.date,
                    timeData.hour, timeData.minute, timeData.second);
            break;

        case RTC_TIME_FORMAT_LOG_FILE:
            /* 日志文件路径格式: YYYY/MM/DD */
            sprintf(buffer, "%04d/%02d/%02d", 
                    timeData.year, timeData.month, timeData.date);
            break;

        case RTC_TIME_FORMAT_LOG_DATETIME:
            /* 日志时间格式: YYYY-MM-DD HH:MM:SS */
            sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", 
                    timeData.year, timeData.month, timeData.date,
                    timeData.hour, timeData.minute, timeData.second);
            break;

        default:
            buffer[0] = '\0';
            break;
    }
}

/**
 * @brief 获取日期字符串
 * @note 格式: YYYY-MM-DD
 * @param[out] buffer 字符串输出缓冲区，至少32字节
 * @return 无
 */
void RTC_Time_GetDateString(char* buffer)
{
    RTC_Time_GetString(buffer, RTC_TIME_FORMAT_DATE);
}

/**
 * @brief 获取时间字符串
 * @note 格式: HH:MM:SS
 * @param[out] buffer 字符串输出缓冲区，至少32字节
 * @return 无
 */
void RTC_Time_GetTimeString(char* buffer)
{
    RTC_Time_GetString(buffer, RTC_TIME_FORMAT_TIME);
}

/**
 * @brief 获取完整日期时间字符串
 * @note 格式: YYYY-MM-DD HH:MM:SS
 * @param[out] buffer 字符串输出缓冲区，至少32字节
 * @return 无
 */
void RTC_Time_GetFullString(char* buffer)
{
    RTC_Time_GetString(buffer, RTC_TIME_FORMAT_FULL);
}

/**
 * @brief 计算Unix时间戳
 * @note 计算自1970-01-01 00:00:00以来的秒数
 *       考虑了闰年和非闰年，以及月份天数
 * @return Unix时间戳(秒)
 * @note 未考虑时区，默认返回UTC时间
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

    /* 计算从1970到指定年份的秒数 */
    for (uint16_t i = 1970; i < y; i++) {
        /* 闰年判断: 能被4整除但不能被100整除，或者能被400整除 */
        if ((i % 4 == 0 && i % 100 != 0) || (i % 400 == 0)) {
            timestamp += 366 * 24 * 3600;  /* 闰年366天 */
        } else {
            timestamp += 365 * 24 * 3600;  /* 非闰年365天 */
        }
    }

    /* 计算从年初到指定月份的秒数 */
    for (uint8_t i = 1; i < m; i++) {
        if (i == 2) {
            /* 2月份需要判断闰年 */
            if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
                timestamp += 29 * 24 * 3600;  /* 闰年2月29天 */
            } else {
                timestamp += 28 * 24 * 3600;  /* 非闰年2月28天 */
            }
        } else {
            timestamp += g_daysOfMonth[i] * 24 * 3600;
        }
    }

    /* 计算当月的秒数 (日期从1开始，需要减1) */
    timestamp += (timeData.date - 1) * 24 * 3600;

    /* 加上当天的秒数 */
    timestamp += timeData.hour * 3600;
    timestamp += timeData.minute * 60;
    timestamp += timeData.second;

    return timestamp;
}

/**
 * @brief 获取星期几的英文名称
 * @param[in] weekDay 星期几(1-7)
 * @return 星期几的英文名称字符串
 * @note 如果参数无效，返回"Unknown"
 */
const char* RTC_Time_GetWeekDayString(uint8_t weekDay)
{
    if (weekDay < RTC_TIME_WEEKDAY_SUNDAY || weekDay > RTC_TIME_WEEKDAY_SATURDAY) {
        return "Unknown";
    }
    return g_weekDayStrings[weekDay];
}

/**
 * @brief 获取月份的英文名称
 * @param[in] month 月份(1-12)
 * @return 月份的英文名称字符串
 * @note 如果参数无效，返回"Unknown"
 */
const char* RTC_Time_GetMonthString(uint8_t month)
{
    if (month < 1 || month > 12) {
        return "Unknown";
    }
    return g_monthStrings[month];
}

/**
 * @brief 手动更新时间
 * @note 供外部定时任务调用
 *       当前为空实现，预留给后续扩展使用
 * @return 无
 */
void RTC_Time_Update(void)
{
}
