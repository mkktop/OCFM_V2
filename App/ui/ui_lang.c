/**
 * @file ui_lang.c
 * @brief 设置菜单中英文翻译模块实现
 *
 * 双语字符串查找表: g_strings[LANG_COUNT][2]
 *   [0] = 英文
 *   [1] = 中文 (UTF-8)
 *
 * 字体选择: 中文模式使用 CJK 字体 (fallback 到 Montserrat)，
 *           英文模式直接使用 Montserrat 字体。
 */

#include "ui_lang.h"
#include "app_config.h"
#include "lvgl.h"

/*============================================================================*/
/*                              字体外部声明                                    */
/*============================================================================*/

/* 自定义 sup3 字体 (Montserrat + superscript 3) */
extern const lv_font_t my_font_montserrat_14;
extern const lv_font_t my_font_montserrat_16;
extern const lv_font_t my_font_montserrat_24;

/* CJK 字体 (仅包含设置菜单所需的汉字, fallback 到 my_font_montserrat) */
extern const lv_font_t noto_sans_sc_16;
extern const lv_font_t noto_sans_sc_24;

/*============================================================================*/
/*                          双语字符串查找表                                   */
/*============================================================================*/

/**
 * @brief 所有可翻译字符串的英中对照表
 *
 * [0] = 英文, [1] = 中文 (UTF-8 编码)
 *
 * @note 中文字符串使用 UTF-8 编码，每个汉字占 3 字节。
 *       例如 "基本" = 0xE5 0x9F 0xBA 0xE6 0x9C 0xAC
 */
static const char * const g_strings[LANG_COUNT][2] = {
    /* 分类名称 */
    [LANG_CAT_BASIC]        = { "Basic",        "\xe5\x9f\xba\xe6\x9c\xac" },              /* 基本 */
    [LANG_CAT_MODBUS]       = { "Modbus",       "\xe4\xb8\xb2\xe5\x8f\xa3\xe8\xae\xbe\xe7\xbd\xae" },              /* 串口设置 */
    [LANG_CAT_ALARM]        = { "Alarm",        "\xe6\x8a\xa5\xe8\xad\xa6" },              /* 报警 */
    [LANG_CAT_CANAL]        = { "Canal",        "\xe6\xb0\xb4\xe6\xb8\xa0" },              /* 水渠 */
    [LANG_CAT_PROFESSIONAL] = { "Professional", "\xe4\xb8\x93\xe4\xb8\x9a" },              /* 专业 */
    [LANG_CAT_DISPLAY]      = { "Display",      "\xe6\x98\xbe\xe7\xa4\xba" },              /* 显示 */
    [LANG_CAT_ADVANCED]     = { "Advanced",     "\xe9\xab\x98\xe7\xba\xa7" },              /* 高级 */
    [LANG_CAT_TIME]         = { "Time",         "\xe6\x97\xb6\xe9\x97\xb4" },              /* 时间 */

    /* 参数名称 - 基本参数 */
    [LANG_P_HEIGHT]         = { "Height",       "\xe9\xab\x98\xe5\xba\xa6" },              /* 高度 */
    [LANG_P_4MA_RANGE]      = { "4mA Range",    "4mA\xe9\x87\x8f\xe7\xa8\x8b" },          /* 4mA量程 */
    [LANG_P_20MA_RANGE]     = { "20mA Range",   "20mA\xe9\x87\x8f\xe7\xa8\x8b" },         /* 20mA量程 */

    /* 参数名称 - 测量参数 */
    [LANG_P_WINDOW_WIDTH]   = { "Window Width", "\xe7\xaa\x97\xe5\x8f\xa3\xe5\xae\xbd\xe5\xba\xa6" }, /* 窗口宽度 */
    [LANG_P_FILTER_COUNT]   = { "Filter Count", "\xe6\xbb\xa4\xe6\xb3\xa2\xe6\xac\xa1\xe6\x95\xb0" }, /* 滤波次数 */
    [LANG_P_SAMPLE_DELAY]   = { "Sample Delay", "\xe9\x87\x87\xe6\xa0\xb7\xe5\xbb\xb6\xe8\xbf\x9f" }, /* 采样延迟 */
    [LANG_P_BLIND_AREA]     = { "Blind Area",   "\xe7\x9b\xb2\xe5\x8c\xba" },              /* 盲区 */
    [LANG_P_WINDOW_COEFF]   = { "Window Coeff", "\xe7\xaa\x97\xe4\xbd\x93\xe7\xb3\xbb\xe6\x95\xb0" }, /* 窗体系数 */
    [LANG_P_MEASURE_COEFF]  = { "Measure Coeff","\xe6\xb5\x8b\xe9\x87\x8f\xe7\xb3\xbb\xe6\x95\xb0" }, /* 测量系数 */

    /* 参数名称 - Modbus参数 */
    [LANG_P_SLAVE_ADDR]     = { "Slave Addr",   "\xe4\xbb\x8e\xe6\x9c\xba\xe5\x9c\xb0\xe5\x9d\x80" }, /* 从机地址 */
    [LANG_P_BAUD_RATE]      = { "Baud Rate",    "\xe6\xb3\xa2\xe7\x89\xb9\xe7\x8e\x87" },  /* 波特率 */
    [LANG_P_STOP_BITS]      = { "Stop Bits",    "\xe5\x81\x9c\xe6\xad\xa2\xe4\xbd\x8d" },  /* 停止位 */

    /* 参数名称 - 报警参数 */
    [LANG_P_ALARM_HIGH]     = { "Alarm High",   "\xe4\xb8\x8a\xe9\x99\x90\xe6\x8a\xa5\xe8\xad\xa6" }, /* 上限报警 */
    [LANG_P_ALARM_LOW]      = { "Alarm Low",    "\xe4\xb8\x8b\xe9\x99\x90\xe6\x8a\xa5\xe8\xad\xa6" }, /* 下限报警 */
    [LANG_P_ALARM_HH]       = { "Alarm HH",     "\xe4\xb8\x8a\xe4\xb8\x8a\xe9\x99\x90\xe6\x8a\xa5\xe8\xad\xa6" },  /* 上上限报警 */
    [LANG_P_ALARM_LL]       = { "Alarm LL",     "\xe4\xb8\x8b\xe4\xb8\x8b\xe9\x99\x90\xe6\x8a\xa5\xe8\xad\xa6" },  /* 下下限报警 */
    [LANG_P_ALARM_HIGH_DB]  = { "Alarm High DB","\xe4\xb8\x8a\xe9\x99\x90\xe5\x9b\x9e\xe5\xb7\xae" }, /* 上限回差 */
    [LANG_P_ALARM_LOW_DB]   = { "Alarm Low DB", "\xe4\xb8\x8b\xe9\x99\x90\xe5\x9b\x9e\xe5\xb7\xae" }, /* 下限回差 */

    /* 参数名称 - 系统设置 */
    [LANG_P_CANAL_TYPE]     = { "Canal Type",   "\xe6\xb0\xb4\xe6\xb8\xa0\xe7\xb1\xbb\xe5\x9e\x8b" }, /* 水渠类型 */
    [LANG_P_CHANNEL_ID]     = { "Channel ID",   "\xe9\x80\x9a\xe9\x81\x93\xe7\xbc\x96\xe5\x8f\xb7" }, /* 通道编号 */
    [LANG_P_SUM_DECIMAL]    = { "Sum Decimal",  "\xe7\xb4\xaf\xe8\xae\xa1\xe5\xb0\x8f\xe6\x95\xb0" }, /* 累计小数 */
    [LANG_P_CLEAR_TOTAL]    = { "Clear Total",  "\xe6\xb8\x85\xe9\x9b\xb6\xe7\xb4\xaf\xe8\xae\xa1" }, /* 清零累计 */
    [LANG_P_TOTAL_FLOW]    = { "Total Flow",   "\xe7\xb4\xaf\xe8\xae\xa1\xe6\xb5\x81\xe9\x87\x8f" }, /* 累计流量 */

    /* 参数名称 - 时间设置 */
    [LANG_P_YEAR]           = { "Year",         "\xe5\xb9\xb4" },                            /* 年 */
    [LANG_P_MONTH]          = { "Month",        "\xe6\x9c\x88" },                            /* 月 */
    [LANG_P_DAY]            = { "Day",          "\xe6\x97\xa5" },                            /* 日 */
    [LANG_P_HOUR]           = { "Hour",         "\xe6\x97\xb6" },                            /* 时 */
    [LANG_P_MINUTE]         = { "Minute",       "\xe5\x88\x86" },                            /* 分 */
    [LANG_P_SECOND]         = { "Second",       "\xe7\xa7\x92" },                            /* 秒 */
    [LANG_P_WEEKDAY]        = { "Weekday",      "\xe6\x98\x9f\xe6\x9c\x9f" },              /* 星期 */

    /* 参数名称 - 显示设置 */
    [LANG_P_LANGUAGE]       = { "Language",     "\xe8\xaf\xad\xe8\xa8\x80" },              /* 语言 */
    [LANG_P_DECIMAL]        = { "Decimal",      "\xe5\xb0\x8f\xe6\x95\xb0\xe4\xbd\x8d\xe6\x95\xb0" }, /* 小数位数 */
    [LANG_P_FLOW_UNIT]      = { "Flow Unit",    "\xe6\xb5\x81\xe9\x87\x8f\xe5\x8d\x95\xe4\xbd\x8d" }, /* 流量单位 */
    [LANG_P_SHOW_ALARM]     = { "Show Alarm",   "\xe6\x98\xbe\xe7\xa4\xba\xe6\x8a\xa5\xe8\xad\xa6" }, /* 显示报警 */

    /* 参数名称 - 高级设置 */
    [LANG_P_RANGE_MAX]      = { "Range Max",    "\xe6\x9c\x80\xe5\xa4\xa7\xe9\x87\x8f\xe7\xa8\x8b" },  /* 最大量程 */
    [LANG_P_ANTENNA_TYPE]   = { "Antenna Type", "\xe5\xa4\xa9\xe7\xba\xbf\xe7\xb1\xbb\xe5\x9e\x8b" }, /* 天线类型 */
    [LANG_P_4MA_CAL]        = { "4mA Cal",      "4mA\xe6\xa0\xa1\xe5\x87\x86" },          /* 4mA校准 */
    [LANG_P_20MA_CAL]       = { "20mA Cal",     "20mA\xe6\xa0\xa1\xe5\x87\x86" },         /* 20mA校准 */
    [LANG_P_DIST_OFFSET]    = { "Dist Offset",  "\xe8\xb7\x9d\xe7\xa6\xbb\xe5\x81\x8f\xe7\xa7\xbb" }, /* 距离偏移 */
    [LANG_P_FACTORY_RESET]  = { "Factory Reset","\xe6\x81\xa2\xe5\xa4\x8d\xe5\x87\xba\xe5\x8e\x82" }, /* 恢复出厂 */
    [LANG_P_PASSWORD]       = { "Password Lock","\xe5\xaf\x86\xe7\xa0\x81\xe9\x94\x81" },            /* 密码锁 */

    /* 格式回调字符串 */
    [LANG_F_ENGLISH]        = { "English",      "\xe8\x8b\xb1\xe6\x96\x87" },              /* 英文 */
    [LANG_F_CHINESE]        = { "Chinese",      "\xe4\xb8\xad\xe6\x96\x87" },              /* 中文 */
    [LANG_F_NONE_1_STOP]    = { "None1StopBits","None1StopBits" },
    [LANG_F_ODD_1_STOP]     = { "Odd1StopBits", "Odd1StopBits" },
    [LANG_F_NONE_2_STOP]    = { "None2StopBits","None2StopBits" },
    [LANG_F_EVEN_1_STOP]    = { "Even1StopBits","Even1StopBits" },
    [LANG_F_PARSHALL_FLUME] = { "ParshallFlume","\xe5\xb7\xb4\xe6\xad\x87\xe5\xb0\x94\xe6\xa7\xbd" }, /* 巴歇尔槽 */
    [LANG_F_TRIANGULAR_WEIR]= { "TriangularWeir","\xe4\xb8\x89\xe8\xa7\x92\xe5\xa0\xb0" }, /* 三角堰 */
    [LANG_F_RECTANGULAR_WEIR]={ "RectangularWeir","\xe7\x9f\xa9\xe5\xbd\xa2\xe5\xa0\xb0" }, /* 矩形堰 */
    [LANG_F_YES]            = { "Yes",          "\xe6\x98\xaf" },                            /* 是 */
    [LANG_F_NO]             = { "No",           "\xe5\x90\xa6" },                            /* 否 */
    [LANG_F_WEEKDAY_SUN]    = { "Sunday",       "\xe5\x91\xa8\xe6\x97\xa5" },              /* 周日 */
    [LANG_F_WEEKDAY_MON]    = { "Monday",       "\xe5\x91\xa8\xe4\xb8\x80" },              /* 周一 */
    [LANG_F_WEEKDAY_TUE]    = { "Tuesday",      "\xe5\x91\xa8\xe4\xba\x8c" },              /* 周二 */
    [LANG_F_WEEKDAY_WED]    = { "Wednesday",    "\xe5\x91\xa8\xe4\xb8\x89" },              /* 周三 */
    [LANG_F_WEEKDAY_THU]    = { "Thursday",     "\xe5\x91\xa8\xe5\x9b\x9b" },              /* 周四 */
    [LANG_F_WEEKDAY_FRI]    = { "Friday",       "\xe5\x91\xa8\xe4\xba\x94" },              /* 周五 */
    [LANG_F_WEEKDAY_SAT]    = { "Saturday",     "\xe5\x91\xa8\xe5\x85\xad" },              /* 周六 */

    /* 历史记录页 */
    [LANG_HIST_TITLE]       = { "History",      "\xe5\x8e\x86\xe5\x8f\xb2\xe6\x9f\xa5\xe8\xaf\xa2" }, /* 历史查询 */
    [LANG_HIST_INST_FLOW]   = { "Inst",         "\xe7\x9e\xac\xe6\x97\xb6" },              /* 瞬时 */
    [LANG_HIST_TOTAL_FLOW]  = { "Total",        "\xe7\xb4\xaf\xe8\xae\xa1" },              /* 累计 */
    [LANG_HIST_NO_DATA]     = { "No Data",      "\xe6\x97\xa0\xe6\x95\xb0\xe6\x8d\xae" },  /* 无数据 */
    [LANG_HIST_LOADING]     = { "Loading...",   "\xe5\x8a\xa0\xe8\xbd\xbd\xe4\xb8\xad" },  /* 加载中 */
    [LANG_HIST_RECORD]      = { "Record",       "\xe8\xae\xb0\xe5\xbd\x95" },              /* 记录 */
    [LANG_HIST_HOUR]        = { "H",            "\xe6\x97\xb6" },                            /* 时 */
};

/*============================================================================*/
/*                              公共 API                                       */
/*============================================================================*/

const char *lang_get(lang_id_t id)
{
    uint8_t lang = app_config_get_language();
    if (lang > 1) lang = 0;
    if (id >= LANG_COUNT) return "";
    return g_strings[id][lang];
}

uint8_t lang_is_chinese(void)
{
    return app_config_get_language() == 1;
}

const void *lang_get_font_14(void)
{
    return &my_font_montserrat_14;
}

const void *lang_get_font_16(void)
{
    return lang_is_chinese() ? &noto_sans_sc_16 : &my_font_montserrat_16;
}

const void *lang_get_font_18(void)
{
    return lang_is_chinese() ? &noto_sans_sc_16 : &lv_font_montserrat_18;
}

const void *lang_get_font_20(void)
{
    return lang_is_chinese() ? &noto_sans_sc_16 : &lv_font_montserrat_20;
}

const void *lang_get_font_24(void)
{
    return lang_is_chinese() ? &noto_sans_sc_24 : &my_font_montserrat_24;
}
