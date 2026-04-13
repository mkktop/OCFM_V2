/**
 * @file ui_lang.h
 * @brief 设置菜单中英文翻译模块
 *
 * 提供简洁的字符串查找表机制，支持设置菜单的中英文切换。
 * 每个可翻译字符串分配一个 lang_id_t 枚举值，
 * 通过 lang_get() 按当前语言配置返回对应字符串。
 */

#ifndef __UI_LANG_H__
#define __UI_LANG_H__

#include <stdint.h>

/**
 * @brief 翻译字符串 ID
 *
 * 每个需要翻译的设置菜单字符串都有一个唯一 ID。
 * [0]=英文, [1]=中文
 */
typedef enum {
    /* 分类名称 */
    LANG_CAT_BASIC = 0,
    LANG_CAT_MODBUS,
    LANG_CAT_ALARM,
    LANG_CAT_CANAL,
    LANG_CAT_PROFESSIONAL,
    LANG_CAT_DISPLAY,
    LANG_CAT_ADVANCED,
    LANG_CAT_TIME,

    /* 参数名称 - 基本参数 */
    LANG_P_HEIGHT,
    LANG_P_4MA_RANGE,
    LANG_P_20MA_RANGE,

    /* 参数名称 - 测量参数 */
    LANG_P_WINDOW_WIDTH,
    LANG_P_FILTER_COUNT,
    LANG_P_SAMPLE_DELAY,
    LANG_P_BLIND_AREA,
    LANG_P_WINDOW_COEFF,
    LANG_P_MEASURE_COEFF,

    /* 参数名称 - Modbus参数 */
    LANG_P_SLAVE_ADDR,
    LANG_P_BAUD_RATE,
    LANG_P_STOP_BITS,

    /* 参数名称 - 报警参数 */
    LANG_P_ALARM_HIGH,
    LANG_P_ALARM_LOW,
    LANG_P_ALARM_HH,
    LANG_P_ALARM_LL,
    LANG_P_ALARM_HIGH_DB,
    LANG_P_ALARM_LOW_DB,

    /* 参数名称 - 系统设置 */
    LANG_P_CANAL_TYPE,
    LANG_P_CHANNEL_ID,
    LANG_P_SUM_DECIMAL,
    LANG_P_CLEAR_TOTAL,

    /* 参数名称 - 时间设置 */
    LANG_P_YEAR,
    LANG_P_MONTH,
    LANG_P_DAY,
    LANG_P_HOUR,
    LANG_P_MINUTE,
    LANG_P_SECOND,
    LANG_P_WEEKDAY,

    /* 参数名称 - 显示设置 */
    LANG_P_LANGUAGE,
    LANG_P_DECIMAL,
    LANG_P_FLOW_UNIT,

    /* 参数名称 - 高级设置 */
    LANG_P_RANGE_MAX,
    LANG_P_ANTENNA_TYPE,
    LANG_P_4MA_CAL,
    LANG_P_20MA_CAL,
    LANG_P_DIST_OFFSET,
    LANG_P_FACTORY_RESET,

    /* 格式回调字符串 */
    LANG_F_ENGLISH,
    LANG_F_CHINESE,
    LANG_F_NONE_1_STOP,
    LANG_F_ODD_1_STOP,
    LANG_F_NONE_2_STOP,
    LANG_F_EVEN_1_STOP,
    LANG_F_PARSHALL_FLUME,
    LANG_F_TRIANGULAR_WEIR,
    LANG_F_RECTANGULAR_WEIR,
    LANG_F_YES,
    LANG_F_NO,
    LANG_F_WEEKDAY_SUN,
    LANG_F_WEEKDAY_MON,
    LANG_F_WEEKDAY_TUE,
    LANG_F_WEEKDAY_WED,
    LANG_F_WEEKDAY_THU,
    LANG_F_WEEKDAY_FRI,
    LANG_F_WEEKDAY_SAT,

    LANG_COUNT
} lang_id_t;

/**
 * @brief 获取翻译字符串
 * @param id 字符串 ID
 * @return 当前语言对应的字符串
 */
const char *lang_get(lang_id_t id);

/**
 * @brief 获取当前语言下的 14px 字体
 * @return 中文模式下返回 CJK 字体，英文模式返回 Montserrat
 */
const void *lang_get_font_14(void);

/**
 * @brief 获取当前语言下的 16px 字体
 */
const void *lang_get_font_16(void);

/**
 * @brief 获取当前语言下的 18px 字体
 */
const void *lang_get_font_18(void);

/**
 * @brief 获取当前语言下的 20px 字体
 */
const void *lang_get_font_20(void);

/**
 * @brief 获取当前语言下的 24px 字体（编辑页用）
 */
const void *lang_get_font_24(void);

/**
 * @brief 判断当前是否为中文模式
 * @return 1=中文, 0=英文
 */
uint8_t lang_is_chinese(void);

#endif /* __UI_LANG_H__ */
