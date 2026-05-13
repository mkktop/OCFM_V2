/**
 * @file ui_set_page.c
 * @brief 设置菜单页面实现
 *
 * ============================================================================
 * 架构概览
 * ============================================================================
 *
 * 数据驱动的二级菜单系统，包含三个导航层级：分类列表 -> 参数列表 -> 编辑页面
 *
 * 导航流程:
 *   主屏幕  --[长按SHIFT]-->  分类列表  --[OK]-->  参数列表
 *        ^                          ^    ^                      ^    ^
 *        |                          |    |                      |    |
 *        +-------- [SHIFT] ---------+    +------- [SHIFT] ------+    |
 *                                                          |         |
 *                                          参数列表 --[OK]--> 编辑页面
 *                                                  ^                 ^    ^
 *                                                  |                 |    |
 *                                                  +-- [OK/SHIFT] ---+    |
 *                                                                        |
 *                                                      参数列表 <--------+
 *
 * 核心设计:
 * 1. 数据驱动: 所有菜单项定义为静态常量表，通过函数指针绑定 app_config 的
 *    getter/setter。新增参数只需在数据表中加一行。
 *
 * 2. 线程安全: 按键回调运行在 button_scan_task (FreeRTOS)，但 LVGL 对象只能
 *    在 LVGL 主任务上下文中修改。所有 UI 操作都通过 lv_async_call() 延迟执行。
 *    按键处理函数只修改普通状态变量(int)，绝不直接操作 LVGL 对象。
 *
 * 3. 屏幕生命周期: 每次导航创建新的 LVGL screen。旧的设置屏幕通过 auto_del
 *    自动释放。主屏幕永远不会被自动释放（通过检查 active_screen 指针判断），
 *    避免返回时访问已释放内存。
 *
 * 4. 过渡保护: g_set_busy 标志在屏幕切换期间屏蔽按键事件，防止重叠的
 *    屏幕 创建/销毁 操作。
 * ============================================================================
 */

#include "ui_set_page.h"
#include "ui.h"
#include "ui_conf.h"
#include "lvgl.h"
#include "app_config.h"
#include "app_flow_calc.h"
#include "app_log.h"
#include "app_current.h"
#include "rtc_time.h"
#include "../../Drivers/Button/button_driver.h"
#include "ui_lang.h"
#include <stdio.h>
#include <string.h>

/*============================================================================*/
/*                            动画配置                                          */
/*============================================================================*/

#define ANIM_TIME           140         /* 屏幕切换动画时长 (ms)       */

/*============================================================================*/
/*                           数据结构定义                                      */
/*============================================================================*/

/**
 * @brief 参数项 - 对应一个可编辑的配置值
 *
 * 每个参数通过函数指针绑定 app_config 的 getter/setter。
 * 菜单系统调用 get() 显示当前值，调用 set() 保存修改到 EEPROM。
 */
typedef struct {
    lang_id_t name_id;          /**< 显示名称 ID (通过 lang_get() 翻译) */
    const char *unit;           /**< 单位字符串 (如 "mm"), 或 ""     */
    uint32_t (*get)(void);      /**< 从配置读取当前值 (uint32路径)   */
    void (*set)(uint32_t);      /**< 写入新值到配置 (uint32路径)     */
    uint32_t min_val;           /**< 允许的最小值 (uint32路径)       */
    uint32_t max_val;           /**< 允许的最大值 (uint32路径)       */
    uint32_t step;              /**< 每次UP/DOWN的调节步进 (uint32)  */
    uint8_t decimal_places;     /**< 小数位数 (0=整数, 3=mm显示为m)  */
    const char *(*format)(uint32_t); /**< 可选: 自定义格式化 (优先于decimal_places) */
    /* --- float 路径 (getf非空时使用) --- */
    float (*getf)(void);        /**< 从配置读取当前值 (float路径)    */
    void (*setf)(float);        /**< 写入新值到配置 (float路径)      */
    float min_valf;             /**< 允许的最小值 (float路径)        */
    float max_valf;             /**< 允许的最大值 (float路径)        */
    float stepf;                /**< 每次UP/DOWN的调节步进 (float)   */
    uint8_t f_decimals;         /**< float 显示的小数位数            */
} set_item_t;

/**
 * @brief 分类 - 一级菜单的一个条目
 *
 * 将相关的参数组织在一起，如"基本参数"、"报警参数"等。
 */
typedef struct {
    lang_id_t name_id;          /**< 分类名称 ID (通过 lang_get() 翻译) */
    const set_item_t *items;    /**< 指向参数数组的指针              */
    uint8_t count;              /**< 参数数量                        */
} set_category_t;

/**
 * @brief 导航层级枚举
 */
typedef enum {
    SET_LEVEL_CATEGORY,         /* 一级菜单 - 分类列表              */
    SET_LEVEL_PARAMETER,        /* 二级菜单 - 参数列表              */
    SET_LEVEL_EDITING,          /* 编辑页面 - 编辑单个参数          */
} set_level_t;

/**
 * @brief 导航状态 - 记录菜单当前位置
 *
 * 由按键处理函数写入 (按键任务上下文)，
 * 由异步回调函数读取 (LVGL上下文)。
 */
typedef struct {
    set_level_t level;          /**< 当前导航层级                    */
    int8_t selected_index;      /**< 当前高亮项索引                  */
    uint32_t edit_value;        /**< 编辑中的临时值 (uint32路径)     */
    float edit_valuef;          /**< 编辑中的临时值 (float路径)      */
    double edit_valued;         /**< 编辑中的临时值 (double路径)     */
    lv_obj_t *current_screen;   /**< 当前显示的屏幕                  */
} set_nav_state_t;

/*============================================================================*/
/*                         菜单数据表 (静态常量)                              */
/*                                                                            */
/* 新增参数的方法:                                                              */
/*   1. 在下方对应的 items[] 数组中添加一行                                    */
/*   2. 对应的 getter/setter 必须已在 app_config.h 中声明                     */
/*   3. 设置合理的 min/max/step 值                                            */
/*============================================================================*/

/**
 * @brief  将整数值格式化为带小数点的字符串
 *
 * @param  value:          原始整数值 (如 mm)
 * @param  decimal_places: 小数位数 (如 3 表示值 5000 显示为 "5.000")
 * @param  buf:            输出缓冲区
 * @param  buf_size:       缓冲区大小
 * @retval buf 指针
 */
static char *format_with_decimal(uint32_t value, uint8_t decimal_places,
                                 char *buf, size_t buf_size)
{
    if (decimal_places == 0) {
        snprintf(buf, buf_size, "%lu", (unsigned long)value);
        return buf;
    }
    uint32_t divisor = 1;
    for (uint8_t i = 0; i < decimal_places; i++) divisor *= 10;
    snprintf(buf, buf_size, "%lu.%0*lu",
             (unsigned long)(value / divisor),
             (int)decimal_places,
             (unsigned long)(value % divisor));
    return buf;
}

/* ---------- 基本参数 ---------- */
static const set_item_t basic_items[] = {
    {LANG_P_HEIGHT,          "m",   app_config_get_height,            app_config_set_height,           0, 20000, 1,   3},
    {LANG_P_4MA_RANGE,       "m\xC2\xB3/h", NULL, NULL, 0, 0, 0, 0, NULL,
                                       app_config_get_range_4ma,  app_config_set_range_4ma,        0.0f, 99999.0f, 0.001f, 3},
    {LANG_P_20MA_RANGE,      "m\xC2\xB3/h", NULL, NULL, 0, 0, 0, 0, NULL,
                                       app_config_get_range_20ma, app_config_set_range_20ma,       0.0f, 99999.0f, 0.001f, 3},
};

/* ---------- 测量参数 ---------- */
static const set_item_t measure_items[] = {
    {LANG_P_WINDOW_WIDTH,    "",     app_config_get_window_width,       app_config_set_window_width,      0, 1000,  1},
    {LANG_P_FILTER_COUNT,    "",     app_config_get_filter_count,       app_config_set_filter_count,      0, 50,    1},
    {LANG_P_SAMPLE_DELAY,    "ms",   app_config_get_delay_time,         app_config_set_delay_time,        0, 1000,  10},
    {LANG_P_BLIND_AREA,      "mm",   app_config_get_blind_area,         app_config_set_blind_area,        0, 1000,  10},
    {LANG_P_WINDOW_COEFF,    "",     app_config_get_w_coeff,            app_config_set_w_coeff,           0, 10,    1},
    {LANG_P_MEASURE_COEFF,   "",     app_config_get_m_coeff,            app_config_set_m_coeff,           0, 10,    1},
};

/* ---------- format 回调前向声明 ---------- */

static const char *format_baudrate(uint32_t val)
{
    switch (val) {
        case 1:  return "4800";
        case 2:  return "9600";
        case 3:  return "14400";
        case 4:  return "19200";
        case 5:  return "38400";
        case 6:  return "56000";
        case 7:  return "57600";
        case 8:  return "115200";
        default: return "9600";
    }
}

static const char *format_stopbits(uint32_t val)
{
    switch (val) {
        case 1:  return lang_get(LANG_F_NONE_1_STOP);
        case 2:  return lang_get(LANG_F_ODD_1_STOP);
        case 3:  return lang_get(LANG_F_NONE_2_STOP);
        case 4:  return lang_get(LANG_F_EVEN_1_STOP);
        default: return lang_get(LANG_F_NONE_1_STOP);
    }
}

/* ---------- Modbus从机参数 ---------- */
static const set_item_t modbus_items[] = {
    {LANG_P_SLAVE_ADDR,      "",     app_config_get_modbus_addr,         app_config_set_modbus_addr,       0, 247,   1},
    {LANG_P_BAUD_RATE,       "",     app_config_get_modbus_baudrate,     app_config_set_modbus_baudrate,   1, 8,     1,  0,  format_baudrate},
    {LANG_P_STOP_BITS,       "",     app_config_get_modbus_stopbits,     app_config_set_modbus_stopbits,   1, 4,     1,  0,  format_stopbits},
};

/* ---------- 报警参数 ---------- */
static const set_item_t alarm_items[] = {
    {LANG_P_ALARM_HIGH,      "m\xC2\xB3/h", NULL, NULL, 0, 0, 0, 0, NULL,
                                       app_config_get_alarm_ah,  app_config_set_alarm_ah,           0.0f, 99999.0f, 0.001f, 3},
    {LANG_P_ALARM_LOW,       "m\xC2\xB3/h", NULL, NULL, 0, 0, 0, 0, NULL,
                                       app_config_get_alarm_al,  app_config_set_alarm_al,           0.0f, 99999.0f, 0.001f, 3},
    {LANG_P_ALARM_HH,        "m\xC2\xB3/h", NULL, NULL, 0, 0, 0, 0, NULL,
                                       app_config_get_alarm_aah, app_config_set_alarm_aah,          0.0f, 99999.0f, 0.001f, 3},
    {LANG_P_ALARM_LL,        "m\xC2\xB3/h", NULL, NULL, 0, 0, 0, 0, NULL,
                                       app_config_get_alarm_aal, app_config_set_alarm_aal,          0.0f, 99999.0f, 0.001f, 3},
    {LANG_P_ALARM_HIGH_DB,   "m\xC2\xB3/h", NULL, NULL, 0, 0, 0, 0, NULL,
                                       app_config_get_alarm_dh,  app_config_set_alarm_dh,           0.0f, 99999.0f, 0.001f, 3},
    {LANG_P_ALARM_LOW_DB,    "m\xC2\xB3/h", NULL, NULL, 0, 0, 0, 0, NULL,
                                       app_config_get_alarm_dl,  app_config_set_alarm_dl,           0.0f, 99999.0f, 0.001f, 3},
};

/* ---------- format 回调函数 ---------- */

static const char *format_language(uint32_t val)
{
    return val == 0 ? lang_get(LANG_F_ENGLISH) : lang_get(LANG_F_CHINESE);
}

static const char *format_flow_unit(uint32_t val)
{
    switch (val) {
        case FLOW_UNIT_L_S:   return "L/s";      /* 升/秒 */
        case FLOW_UNIT_L_MIN: return "L/min";    /* 升/分钟 */
        case FLOW_UNIT_L_H:   return "L/h";      /* 升/小时 */
        case FLOW_UNIT_M3_H:  return "m\xC2\xB3/h";     /* 立方米/小时 */
        case FLOW_UNIT_M3_S:  return "m\xC2\xB3/s";     /* 立方米/秒 */
        case FLOW_UNIT_M3_MIN:return "m\xC2\xB3/min";   /* 立方米/分钟 */
        case FLOW_UNIT_T_H:   return "T/h";      /* 吨/小时 */
        case FLOW_UNIT_G_H:   return "G/h";      /* 美制加仑/小时 */
        default: return "L/s";
    }
}

static const char *format_canals_type(uint32_t val)
{
    switch (val) {
        case 1:  return lang_get(LANG_F_PARSHALL_FLUME);
        case 2:  return lang_get(LANG_F_TRIANGULAR_WEIR);
        case 3:  return lang_get(LANG_F_RECTANGULAR_WEIR);
        default: return lang_get(LANG_F_PARSHALL_FLUME);
    }
}

static const char *format_weekday(uint32_t val)
{
    if (val < 1 || val > 7) return "";
    return lang_get((lang_id_t)(LANG_F_WEEKDAY_SUN + val - 1));
}

/* ---------- RTC时间 getter/setter ---------- */

static uint32_t rtc_get_year(void)    { RTC_TimeData t; RTC_Time_Get(&t); return t.year; }
static uint32_t rtc_get_month(void)   { RTC_TimeData t; RTC_Time_Get(&t); return t.month; }
static uint32_t rtc_get_day(void)     { RTC_TimeData t; RTC_Time_Get(&t); return t.date; }
static uint32_t rtc_get_hour(void)    { RTC_TimeData t; RTC_Time_Get(&t); return t.hour; }
static uint32_t rtc_get_minute(void)  { RTC_TimeData t; RTC_Time_Get(&t); return t.minute; }
static uint32_t rtc_get_second(void)  { RTC_TimeData t; RTC_Time_Get(&t); return t.second; }
static uint32_t rtc_get_weekday(void) { RTC_TimeData t; RTC_Time_Get(&t); return t.weekDay; }

static void rtc_set_year(uint32_t v)    { RTC_TimeData t; RTC_Time_Get(&t); t.year = (uint16_t)v; RTC_Time_Set(&t); }
static void rtc_set_month(uint32_t v)   { RTC_TimeData t; RTC_Time_Get(&t); t.month = (uint8_t)v; RTC_Time_Set(&t); }
static void rtc_set_day(uint32_t v)     { RTC_TimeData t; RTC_Time_Get(&t); t.date = (uint8_t)v; RTC_Time_Set(&t); }
static void rtc_set_hour(uint32_t v)    { RTC_TimeData t; RTC_Time_Get(&t); t.hour = (uint8_t)v; RTC_Time_Set(&t); }
static void rtc_set_minute(uint32_t v)  { RTC_TimeData t; RTC_Time_Get(&t); t.minute = (uint8_t)v; RTC_Time_Set(&t); }
static void rtc_set_second(uint32_t v)  { RTC_TimeData t; RTC_Time_Get(&t); t.second = (uint8_t)v; RTC_Time_Set(&t); }
static void rtc_set_weekday(uint32_t v) { RTC_TimeData t; RTC_Time_Get(&t); t.weekDay = (uint8_t)v; RTC_Time_Set(&t); }

/* ---------- 清除累计流量 ---------- */

static uint32_t clear_total_flow_get(void) { return 0; }
static void clear_total_flow_set(uint32_t v) { if (v == 1) flow_calc_reset_total(); }
static const char *format_yes_no(uint32_t val) { return val == 1 ? lang_get(LANG_F_YES) : lang_get(LANG_F_NO); }

/* ---------- 清除SD卡数据 ---------- */

static uint32_t clear_sd_data_get(void) { return 0; }

static void clear_sd_data_set(uint32_t v)
{
    if (v == 1) {
        app_log_request_clear_sd();
    }
}

/* ---------- 累计流量设置 (double 路径) ---------- */
static char total_flow_fmt_buf[24];
static float total_flow_getf(void) { return (float)flow_calc_get_total(); }
static void total_flow_setf(float v) { flow_calc_set_total((double)v); }
static uint32_t total_flow_get_dummy(void) { return 0; }
static const char *format_total_flow(uint32_t val) {
    (void)val;
    snprintf(total_flow_fmt_buf, sizeof(total_flow_fmt_buf),
             "%.*lf", (int)app_config_get_sum_point(), flow_calc_get_total());
    return total_flow_fmt_buf;
}
static uint8_t is_total_flow_item(const set_item_t *item)
{
    return (item->setf == total_flow_setf);
}

/* ---------- 小数位数 (循环) ---------- */
static char decimal_buf[4];
static const char *format_decimal(uint32_t val)
{
    snprintf(decimal_buf, sizeof(decimal_buf), "%lu", (unsigned long)val);
    return decimal_buf;
}

/* ---------- 通道编号动态范围 ---------- */

/**
 * @brief  根据当前水渠类型获取通道编号最大值
 */
static uint32_t get_channel_id_max(void)
{
    switch (app_config_get_canals_type()) {
        case 2:  return 5;    /* 三角堰 */
        case 3:  return 4;    /* 矩形堰 */
        default: return 16;   /* 巴歇尔槽 */
    }
}

/**
 * @brief  获取当前编辑项的有效最大值
 * @note   channel_id 的最大值取决于水渠类型
 */
static uint32_t get_effective_max(const set_item_t *item)
{
    if (item->set == app_config_set_channel_id) {
        return get_channel_id_max();
    }
    if (item->set == app_config_set_height) {
        uint32_t range_max = app_config_get_range_max();
        return (range_max < item->max_val) ? range_max : item->max_val;
    }
    return item->max_val;
}

static const char *format_channel_id(uint32_t val)
{
    snprintf(decimal_buf, sizeof(decimal_buf), "%lu", (unsigned long)val);
    return decimal_buf;
}

/* ---------- 系统设置 ---------- */
static const set_item_t system_items[] = {
    {LANG_P_CANAL_TYPE,      "",     app_config_get_canals_type,         app_config_set_canals_type,       1, 3,     1,  0,  format_canals_type},
    {LANG_P_CHANNEL_ID,      "",     app_config_get_channel_id,          app_config_set_channel_id,        1, 16,   1},
    {LANG_P_TOTAL_FLOW,      "m\xC2\xB3", total_flow_get_dummy, NULL, 0, 0, 0, 0, format_total_flow,
                                       total_flow_getf, total_flow_setf, 0.0f, 999999999999.0f, 0.001f, 3},
    {LANG_P_SUM_DECIMAL,     "",     app_config_get_sum_point,           app_config_set_sum_point,          0, 3,     1},
    {LANG_P_CLEAR_TOTAL,     "",     clear_total_flow_get,               clear_total_flow_set,              0, 1,     1,  0,  format_yes_no},
};
static const set_item_t time_items[] = {
    {LANG_P_YEAR,       "",  rtc_get_year,    rtc_set_year,    2000, 2099, 1},
    {LANG_P_MONTH,      "",  rtc_get_month,   rtc_set_month,   1,    12,   1},
    {LANG_P_DAY,        "",  rtc_get_day,     rtc_set_day,     1,    31,   1},
    {LANG_P_HOUR,       "",  rtc_get_hour,    rtc_set_hour,    0,    23,   1},
    {LANG_P_MINUTE,     "",  rtc_get_minute,  rtc_set_minute,  0,    59,   1},
    {LANG_P_SECOND,     "",  rtc_get_second,  rtc_set_second,  0,    59,   1},
    {LANG_P_WEEKDAY,    "",  rtc_get_weekday, rtc_set_weekday, 1,    7,   1,  0,  format_weekday},
};

/* ---------- 显示设置 ---------- */
static const set_item_t display_items[] = {
    {LANG_P_LANGUAGE,        "",     app_config_get_language,            app_config_set_language,           0, 1,     1,  0,  format_language},
    {LANG_P_DECIMAL,         "",     app_config_get_point_num,          app_config_set_point_num,          0, 3,     1,  0,  format_decimal},
    {LANG_P_FLOW_UNIT,       "",     app_config_get_instant_unit,        app_config_set_instant_unit,      1, 8,     1,  0,  format_flow_unit},
    {LANG_P_SHOW_ALARM,      "",     app_config_get_show_alarm,          app_config_set_show_alarm,         0, 1,     1,  0,  format_yes_no},
};

/* ---------- 高级设置 ---------- */
static const set_item_t advanced_items[] = {
    {LANG_P_RANGE_MAX,       "m",   app_config_get_range_max,            app_config_set_range_max,        0, 20000, 1,   3},
    {LANG_P_ANTENNA_TYPE,    "",     app_config_get_antenna_type,        app_config_set_antenna_type,     0, 10,    1},
    {LANG_P_4MA_CAL,         "",     app_config_get_calibration_4ma,     app_config_set_calibration_4ma, 500, 2000, 1},
    {LANG_P_20MA_CAL,        "",     app_config_get_calibration_20ma,    app_config_set_calibration_20ma, 3000, 7000, 1},
    {LANG_P_DIST_OFFSET,     "mm",   app_config_get_dis_offset,          app_config_set_dis_offset,       0, 99999, 10},
    {LANG_P_PASSWORD,        "",     app_config_get_password_enable,      app_config_set_password_enable,   0, 1,     1,  0,  format_yes_no},
    {LANG_P_FACTORY_RESET,   "",     app_config_get_factory_settings,    app_config_set_factory_settings,  0, 1,     1,  0,  format_yes_no},
    {LANG_P_CLEAR_SD_DATA,   "",     clear_sd_data_get,                  clear_sd_data_set,                0, 1,     1,  0,  format_yes_no},
};

/* ---------- 一级菜单分类表 ---------- */
static const set_category_t categories[] = {
    {LANG_CAT_BASIC,        basic_items,    sizeof(basic_items) / sizeof(basic_items[0])},
    {LANG_CAT_MODBUS,       modbus_items,   sizeof(modbus_items) / sizeof(modbus_items[0])},
    {LANG_CAT_ALARM,        alarm_items,    sizeof(alarm_items) / sizeof(alarm_items[0])},
    {LANG_CAT_CANAL,        system_items,   sizeof(system_items) / sizeof(system_items[0])},
    {LANG_CAT_PROFESSIONAL, measure_items,  sizeof(measure_items) / sizeof(measure_items[0])},
    {LANG_CAT_DISPLAY,      display_items,  sizeof(display_items) / sizeof(display_items[0])},
    {LANG_CAT_ADVANCED,     advanced_items, sizeof(advanced_items) / sizeof(advanced_items[0])},
    {LANG_CAT_TIME,         time_items,     sizeof(time_items) / sizeof(time_items[0])},
};

#define CATEGORY_COUNT (sizeof(categories) / sizeof(categories[0]))

/*============================================================================*/
/*                              模块变量                                       */
/*                                                                            */
/* g_set_nav:        导航状态，记录当前位置和当前屏幕                          */
/* g_category_index: 记住当前选中的分类索引，从参数列表/编辑页返回分类列表时     */
/*                   用于恢复正确的分类位置                                     */
/* g_set_busy:       过渡保护标志。屏幕切换期间设为1，屏蔽按键事件，             */
/*                   防止重叠的屏幕创建/销毁操作                                */
/* g_edit_value_label: 编辑页面中大字值标签的直接指针，用于异步回调更新         */
/*                   显示值而无需遍历对象树。离开编辑页时置NULL防止悬空指针       */
/*============================================================================*/

static set_nav_state_t g_set_nav;
static int8_t g_category_index;
static volatile uint8_t g_set_busy;
static lv_obj_t *g_edit_value_label = NULL;   /* 编辑页面大字值标签指针 */
static const set_item_t *g_edit_item = NULL;   /* 当前编辑的参数项指针 */
static int8_t g_category_style_index = -1;    /* Last styled category row */
static int8_t g_parameter_style_index = -1;   /* Last styled parameter row */
static uint32_t g_step_list[5];               /* 步进值列表 (1,10,100,1000,10000) */
static float g_stepf_list[13];                /* float步进值列表 */
static double g_stepd_list[15];               /* double步进值列表 (累计流量) */
static uint8_t g_step_count;                  /* 当前参数的步进级数 */
static uint8_t g_step_index;                  /* 当前选中的步进索引 */
static uint32_t g_last_key_tick;              /* 最后一次按键操作的 tick */
static lv_timer_t *g_idle_timer = NULL;       /* 空闲超时检测定时器 */
static uint8_t g_is_cal_edit = 0;             /* 当前编辑的是校准参数 */
static lv_timer_t *g_clear_sd_timer = NULL;   /* SD clear progress timer */

#define IDLE_TIMEOUT_MS       15000            /* 15秒无操作自动返回主页 */
#define IDLE_CHECK_PERIOD_MS  1000             /* 每秒检查一次 */

/*============================================================================*/
/*                          异步上下文结构体                                   */
/*                                                                            */
/* 这些结构体用于在按键处理函数(按键任务)和异步回调(LVGL任务)之间              */
/* 传递数据。使用 lv_malloc() 分配，回调使用后 lv_free() 释放。                */
/*============================================================================*/

/** 屏幕导航上下文 (进入分类/参数/编辑页) */
typedef struct {
    set_level_t target_level;   /**< 目标层级                        */
    int8_t category_idx;        /**< 目标分类索引                    */
    int8_t item_idx;            /**< 目标参数索引                    */
} set_nav_context_t;

/** 选中项更新上下文 */
typedef struct {
    int8_t new_index;           /**< 新的选中索引                    */
} set_select_context_t;

/** 编辑值更新上下文 */
typedef struct {
    uint32_t new_value;         /**< 新的编辑值 (uint32路径)         */
    float new_valuef;           /**< 新的编辑值 (float路径)          */
    double new_valued;          /**< 新的编辑值 (double路径)         */
} set_edit_val_context_t;

/** 步进切换上下文 */
typedef struct {
    uint8_t new_step_index;     /**< 新的步进索引                    */
} set_step_context_t;

/*============================================================================*/
/*                          私有函数声明                                       */
/*============================================================================*/

/* --- 屏幕创建函数 (仅限LVGL上下文调用) --- */
static lv_obj_t *create_category_screen(void);
static lv_obj_t *create_parameter_screen(uint8_t cat_idx);
static lv_obj_t *create_edit_screen(uint8_t cat_idx, uint8_t item_idx);

/* --- 选中高亮更新函数 (仅限LVGL上下文调用) --- */
static void update_category_selection(void);
static void update_parameter_selection(void);
static void update_edit_value_display(void);
static uint8_t generate_step_list(uint32_t max_val);
static void update_category_row(lv_obj_t *list, int8_t row_idx, uint8_t selected);
static void update_parameter_row(lv_obj_t *list, int8_t row_idx, uint8_t selected);
static void update_clear_sd_progress_display(int8_t progress);

/* --- 按键处理函数 (仅限按键任务上下文调用，不含LVGL操作) --- */
static void handle_category_key(uint8_t button_id);
static void handle_parameter_key(uint8_t button_id);
static void handle_edit_key(uint8_t button_id, uint8_t event);

/* --- 异步回调函数 (在LVGL上下文中执行) --- */
static void async_enter_category_cb(void *context);
static void async_enter_parameter_cb(void *context);
static void async_enter_edit_cb(void *context);
static void async_exit_to_main_cb(void *context);
static void async_select_category_cb(void *context);
static void async_select_parameter_cb(void *context);
static void async_update_edit_val_cb(void *context);
static void async_update_step_cb(void *context);
static void async_start_clear_sd_cb(void *context);

/* --- 屏幕切换辅助函数 --- */
static void set_screen_load(lv_obj_t *new_screen, lv_screen_load_anim_t anim, uint32_t time);
static void idle_timeout_cb(lv_timer_t *timer);
static void clear_sd_progress_timer_cb(lv_timer_t *timer);

/**
 * @brief  判断当前编辑项是否为校准参数
 */
static uint8_t is_calibration_item(const set_item_t *item)
{
    return (item->set == app_config_set_calibration_4ma ||
            item->set == app_config_set_calibration_20ma);
}

/**
 * @brief  判断当前编辑项是否为清除SD卡数据
 */
static uint8_t is_clear_sd_item(const set_item_t *item)
{
    return (item != NULL && item->set == clear_sd_data_set);
}

/*============================================================================*/
/*                          公共函数实现                                       */
/*============================================================================*/

/**
 * @brief  进入设置页面 - 从主屏幕调用 (长按SHIFT)
 *
 * 重置导航状态，异步创建分类列表屏幕。
 * 设置 g_set_busy 阻止过渡动画期间的按键事件。
 */
void set_page_enter(void)
{
    g_set_busy = 0;                                   /* 先清零，防止上次异常退出后残留导致按键永久屏蔽 */
    g_last_key_tick = lv_tick_get();                  /* 记录进入时间 */

    /* 创建空闲超时定时器 */
    if (g_idle_timer == NULL) {
        g_idle_timer = lv_timer_create(idle_timeout_cb, IDLE_CHECK_PERIOD_MS, NULL);
    }
    lv_timer_set_period(g_idle_timer, IDLE_CHECK_PERIOD_MS);
    lv_timer_ready(g_idle_timer);
    g_set_nav.level = SET_LEVEL_CATEGORY;             /* 导航层级重置为一级菜单(分类列表) */
    g_set_nav.selected_index = 0;                     /* 选中项归零，默认高亮第一个分类 */
    g_set_nav.current_screen = NULL;                  /* 清空屏幕指针，防止指向已释放的旧屏幕 */

    set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
    if (ctx == NULL) return;
    ctx->target_level = 0;
    ctx->category_idx = 0;
    ctx->item_idx = 0;
    g_set_busy = 1;                                   /* 设置忙标志，屏蔽过渡动画期间的按键事件 */
    lv_async_call(async_enter_category_cb, ctx);
    /* 注意: g_set_busy 在 async_enter_category_cb 回调中重置为0 */
}

/**
 * @brief  退出设置页面，返回主屏幕 (分类列表中按SHIFT)
 *
 * 异步加载主屏幕。当前设置屏幕在过渡动画完成后自动释放。
 */
void set_page_exit(void)
{
    g_set_busy = 1;
    if (g_idle_timer) {
        lv_timer_del(g_idle_timer);
        g_idle_timer = NULL;
    }
    if (g_is_cal_edit) { g_is_cal_edit = 0; app_current_exit_calibration(); }
    lv_async_call(async_exit_to_main_cb, NULL);
}

/**
 * @brief  设置页面按键事件分发入口
 *
 * 从 app_button.c 调用。运行在 button_scan_task (FreeRTOS) 中，
 * 不在 LVGL 上下文中。因此本函数只更新状态和队列异步调用，
 * 绝不直接修改 LVGL 对象。
 *
 * @param  button_id: BUTTON_ID_OK/UP/DOWN/SHIFT
 * @param  event:     BUTTON_EVENT_SHORT (设置中忽略长按)
 */
void set_page_button_handler(uint8_t button_id, uint8_t event)
{
    g_last_key_tick = lv_tick_get();  /* 刷新最后操作时间 */

    /* 编辑页长按 SHIFT 返回上级，放行此事件 */
    if (event != BUTTON_EVENT_SHORT) {
        if (g_set_nav.level == SET_LEVEL_EDITING &&
            button_id == BUTTON_ID_SHIFT && event == BUTTON_EVENT_LONG && !g_set_busy) {
            handle_edit_key(button_id, event);
        }
        return;
    }
    if (g_set_busy) return;   /* 过渡动画期间屏蔽按键 */

    switch (g_set_nav.level) {
    case SET_LEVEL_CATEGORY:
        handle_category_key(button_id);
        break;
    case SET_LEVEL_PARAMETER:
        handle_parameter_key(button_id);
        break;
    case SET_LEVEL_EDITING:
        handle_edit_key(button_id, event);
        break;
    default:
        break;
    }
}

/*============================================================================*/
/*                          一级菜单 - 分类列表                                */
/*                                                                            */
/* 显示5个分类 (Basic, Measure, Modbus, Alarm, System)。                     */
/* UP/DOWN 高亮选择分类，OK 进入对应参数列表，SHIFT 返回主屏幕。              */
/*============================================================================*/

/**
 * @brief  创建分类列表屏幕 (一级菜单)
 *
 * 屏幕布局 (320x240):
 *   +------------------------------------------+
 *   | < Settings              SHIFT:Back       |  顶栏 36px
 *   +------------------------------------------+
 *   | > Basic                            [7]  |  选中行 (高亮)
 *   |   Measure                          [7]  |
 *   |   Modbus                          [3]  |
 *   |   Alarm                            [6]  |
 *   |   System                           [7]  |
 *   +------------------------------------------+
 *
 * @note  创建 screen 后立即设置 g_set_nav.current_screen = screen，
 *        确保后续的 update_category_selection() 操作的是正确的屏幕对象。
 */
static lv_obj_t *create_category_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    if (screen == NULL) return NULL;
    ui_container_style_init(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_size(screen, LV_HOR_RES, LV_VER_RES);

    /* 关键: 在调用 update_category_selection() 之前设置 current_screen */
    g_set_nav.current_screen = screen;
    g_category_style_index = -1;

    /* --- 顶栏 --- */
    lv_obj_t *top_bar = lv_obj_create(screen);
    ui_container_style_init(top_bar);
    lv_obj_set_size(top_bar, LV_HOR_RES, 36);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(top_bar, 10, 0);

    lv_obj_t *title_label = lv_label_create(top_bar);
    lv_label_set_text(title_label, LV_SYMBOL_LEFT "  Settings");
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(title_label, lang_get_font_20(), 0);

    lv_obj_t *spacer = lv_obj_create(top_bar);
    ui_container_style_init(spacer);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_height(spacer, 1);

    lv_obj_t *back_label = lv_label_create(top_bar);
    lv_label_set_text(back_label, "SHIFT:Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(back_label, lang_get_font_14(), 0);
    lv_obj_set_style_pad_right(back_label, 10, 0);

    /* --- 可滚动列表容器 --- */
    lv_obj_t *list = lv_obj_create(screen);
    ui_container_style_init(list);
    lv_obj_set_size(list, LV_HOR_RES, LV_VER_RES - 36);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_style_max_height(list, LV_VER_RES - 36, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    /* --- 为每个分类创建一行 --- */
    char buf[16];
    for (uint8_t i = 0; i < CATEGORY_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(list);
        ui_container_style_init(row);
        lv_obj_set_size(row, LV_HOR_RES, 40);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(row, 15, 0);
        lv_obj_set_style_pad_right(row, 15, 0);

        /* 行子对象布局: [0]名称 [1]弹性空间 [2]箭头 [3]参数数量 */
        lv_obj_t *name_label = lv_label_create(row);
        lv_label_set_text(name_label, lang_get(categories[i].name_id));
        lv_obj_set_style_text_font(name_label, lang_get_font_18(), 0);
        lv_obj_set_style_text_color(name_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);

        lv_obj_t *spacer = lv_obj_create(row);
        ui_container_style_init(spacer);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_grow(spacer, 1);
        lv_obj_set_height(spacer, 1);

        lv_obj_t *arrow = lv_label_create(row);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, lv_color_hex(COLOR_TEXT_NORMAL), 0);

        lv_obj_t *cnt = lv_label_create(row);
        snprintf(buf, sizeof(buf), "[%d]", categories[i].count);
        lv_label_set_text(cnt, buf);
        lv_obj_set_style_text_color(cnt, lv_color_hex(COLOR_TEXT_NORMAL), 0);
        lv_obj_set_style_text_font(cnt, lang_get_font_14(), 0);
        lv_obj_set_style_margin_right(cnt, 5, 0);
    }

    return screen;
}

/**
 * @brief  更新分类列表选中高亮
 *
 * 遍历分类列表的所有行，为选中行应用高亮样式 (白色文字+灰色背景)，
 * 其他行应用普通样式 (灰色文字+透明背景)。同时滚动确保选中行可见。
 *
 * 必须在LVGL上下文中调用 (通过异步回调)。
 * 读取 g_set_nav.current_screen 和 g_set_nav.selected_index。
 */
static void update_category_selection(void)
{
    lv_obj_t *screen = g_set_nav.current_screen;
    if (screen == NULL) return;

    /* 子对象索引: 0=顶栏, 1=列表容器 */
    lv_obj_t *list = lv_obj_get_child(screen, 1);
    if (list == NULL) return;

    int8_t selected = g_set_nav.selected_index;
    if (selected < 0 || selected >= (int8_t)CATEGORY_COUNT) return;

    if (g_category_style_index < 0) {
        for (uint8_t i = 0; i < CATEGORY_COUNT; i++) {
            update_category_row(list, (int8_t)i, i == (uint8_t)selected);
        }
    } else if (g_category_style_index != selected) {
        update_category_row(list, g_category_style_index, 0);
        update_category_row(list, selected, 1);
    } else {
        update_category_row(list, selected, 1);
    }
    g_category_style_index = selected;

    lv_obj_t *row = lv_obj_get_child(list, selected);
    if (row) lv_obj_scroll_to_view(row, LV_ANIM_OFF);
}

static void update_category_row(lv_obj_t *list, int8_t row_idx, uint8_t selected)
{
    if (list == NULL || row_idx < 0 || row_idx >= (int8_t)CATEGORY_COUNT) return;

    lv_obj_t *row = lv_obj_get_child(list, row_idx);
    if (row == NULL) return;

    lv_obj_t *name_lbl = lv_obj_get_child(row, 0);
    lv_obj_t *arrow_lbl = lv_obj_get_child(row, 2);
    lv_obj_t *cnt_lbl = lv_obj_get_child(row, 3);
    if (name_lbl == NULL || arrow_lbl == NULL) return;

    if (selected) {
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ROW_SEL), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(COLOR_TEXT_SEL), 0);
        lv_obj_set_style_text_color(arrow_lbl, lv_color_hex(COLOR_ACCENT), 0);
        if (cnt_lbl) lv_obj_set_style_text_color(cnt_lbl, lv_color_hex(COLOR_TEXT_SEL), 0);
    } else {
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(COLOR_TEXT_NORMAL), 0);
        lv_obj_set_style_text_color(arrow_lbl, lv_color_hex(COLOR_TEXT_NORMAL), 0);
        if (cnt_lbl) lv_obj_set_style_text_color(cnt_lbl, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    }
}

/**
 * @brief  分类列表按键处理 (一级菜单)
 *
 * 运行在按键任务上下文。只更新状态和队列异步调用。
 * 不包含任何直接的 LVGL 对象操作。
 *
 * UP/DOWN:  修改 g_set_nav.selected_index，队列异步高亮更新
 * OK:       保存分类索引，队列异步创建参数列表屏幕
 * SHIFT:    队列异步返回主屏幕
 */
static void handle_category_key(uint8_t button_id)
{
    switch (button_id) {
    case BUTTON_ID_UP:
        if (g_set_nav.selected_index > 0) {
            g_set_nav.selected_index--;
            set_select_context_t *ctx = lv_malloc(sizeof(set_select_context_t));
            if (ctx) { ctx->new_index = g_set_nav.selected_index; lv_async_call(async_select_category_cb, ctx); }
        }
        break;
    case BUTTON_ID_DOWN:
        if (g_set_nav.selected_index < (int8_t)(CATEGORY_COUNT - 1)) {
            g_set_nav.selected_index++;
            set_select_context_t *ctx = lv_malloc(sizeof(set_select_context_t));
            if (ctx) { ctx->new_index = g_set_nav.selected_index; lv_async_call(async_select_category_cb, ctx); }
        }
        break;
    case BUTTON_ID_OK:
        /* 保存选中的分类索引 (跨层级保持) */
        g_category_index = g_set_nav.selected_index;
        {
            set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
            if (ctx) {
                ctx->target_level = 0; ctx->category_idx = g_category_index; ctx->item_idx = 0;
                g_set_busy = 1;  /* 过渡期间屏蔽按键 */
                lv_async_call(async_enter_parameter_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_SHIFT:
        set_page_exit();
        break;
    default:
        break;
    }
}

/*============================================================================*/
/*                          二级菜单 - 参数列表                                */
/*                                                                            */
/* 显示选中分类下的所有参数。每行左侧为参数名称，右侧为当前值和单位。        */
/* UP/DOWN 高亮选择参数，OK 进入编辑页面，SHIFT 返回分类列表。               */
/*============================================================================*/

/**
 * @brief  创建参数列表屏幕 (二级菜单)
 *
 * 屏幕布局 (320x240):
 *   +------------------------------------------+
 *   | < Basic                  SHIFT:Back       |  顶栏 36px
 *   +------------------------------------------+
 *   | > Range Max             5000  mm         |  选中行 (高亮)
 *   |   Height                5000  mm         |
 *   |   4mA Cal               313             |
 *   |   ...                                    |  (可滚动)
 *   +------------------------------------------+
 *
 * @param  cat_idx: 分类索引 (0-4)，对应 categories[] 数组下标
 */
static lv_obj_t *create_parameter_screen(uint8_t cat_idx)
{
    const set_category_t *cat = &categories[cat_idx];

    lv_obj_t *screen = lv_obj_create(NULL);
    if (screen == NULL) return NULL;
    ui_container_style_init(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_size(screen, LV_HOR_RES, LV_VER_RES);

    /* 关键: 在调用 update_parameter_selection() 之前设置 current_screen */
    g_set_nav.current_screen = screen;
    g_parameter_style_index = -1;

    /* --- 顶栏 (与分类列表结构相同) --- */
    lv_obj_t *top_bar = lv_obj_create(screen);
    ui_container_style_init(top_bar);
    lv_obj_set_size(top_bar, LV_HOR_RES, 36);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(top_bar, 10, 0);

    lv_obj_t *title_label = lv_label_create(top_bar);
    char title_buf[32];
    snprintf(title_buf, sizeof(title_buf), LV_SYMBOL_LEFT "  %s", lang_get(cat->name_id));
    lv_label_set_text(title_label, title_buf);
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(title_label, lang_get_font_20(), 0);

    lv_obj_t *spacer = lv_obj_create(top_bar);
    ui_container_style_init(spacer);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_height(spacer, 1);

    lv_obj_t *back_label = lv_label_create(top_bar);
    lv_label_set_text(back_label, "SHIFT:Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(back_label, lang_get_font_14(), 0);
    lv_obj_set_style_pad_right(back_label, 10, 0);

    /* --- 可滚动列表容器 --- */
    lv_obj_t *list = lv_obj_create(screen);
    if (list == NULL) { lv_obj_del(screen); return NULL; }
    ui_container_style_init(list);
    lv_obj_set_size(list, LV_HOR_RES, LV_VER_RES - 36);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_style_max_height(list, LV_VER_RES - 36, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    /* --- 为每个参数创建一行 --- */
    char val_buf[24];
    for (uint8_t i = 0; i < cat->count; i++) {
        const set_item_t *item = &cat->items[i];

        lv_obj_t *row = lv_obj_create(list);
        ui_container_style_init(row);
        lv_obj_set_size(row, LV_HOR_RES, 36);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(row, 15, 0);
        lv_obj_set_style_pad_right(row, 15, 0);

        /*
         * 行子对象布局:
         *   [0] 参数名  [1] 弹性空间(grow)  [2] 参数值  [3] 单位(可选)
         */
        lv_obj_t *name_label = lv_label_create(row);
        lv_label_set_text(name_label, lang_get(item->name_id));
        lv_obj_set_style_text_font(name_label, lang_get_font_16(), 0);
        lv_obj_set_style_text_color(name_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);

        lv_obj_t *spacer = lv_obj_create(row);
        ui_container_style_init(spacer);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_grow(spacer, 1);
        lv_obj_set_height(spacer, 1);

        /* 显示当前配置值 (从 app_config 内存副本读取) */
        lv_obj_t *val_label = lv_label_create(row);
        if (item->format) {
            lv_label_set_text(val_label, item->format(item->get()));
        } else if (item->getf) {
            snprintf(val_buf, sizeof(val_buf), "%.*f",
                     (int)item->f_decimals, item->getf());
            lv_label_set_text(val_label, val_buf);
        } else if (item->decimal_places > 0) {
            format_with_decimal(item->get(), item->decimal_places,
                               val_buf, sizeof(val_buf));
            lv_label_set_text(val_label, val_buf);
        } else {
            snprintf(val_buf, sizeof(val_buf), "%lu", (unsigned long)item->get());
            lv_label_set_text(val_label, val_buf);
        }
        lv_obj_set_style_text_font(val_label, lang_get_font_16(), 0);
        lv_obj_set_style_text_color(val_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);

        /* 单位标签 (仅当单位字符串非空时创建) */
        if (item->unit && item->unit[0] != '\0') {
            lv_obj_t *unit_label = lv_label_create(row);
            lv_label_set_text(unit_label, item->unit);
            lv_obj_set_style_text_font(unit_label, lang_get_font_14(), 0);
            lv_obj_set_style_text_color(unit_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
            lv_obj_set_style_margin_left(unit_label, 5, 0);
        }
    }

    return screen;
}

/**
 * @brief  更新参数列表选中高亮
 *
 * 与 update_category_selection() 模式相同，但作用于参数行。
 * 注意: 有些行有3个子对象 (无单位)，有些有4个 (有单位)，
 * 因此在访问单位标签前需检查 lv_obj_get_child_cnt()。
 */
static void update_parameter_selection(void)
{
    lv_obj_t *screen = g_set_nav.current_screen;
    if (screen == NULL) return;

    const set_category_t *cat = &categories[g_category_index];
    lv_obj_t *list = lv_obj_get_child(screen, 1);
    if (list == NULL) return;

    int8_t selected = g_set_nav.selected_index;
    if (selected < 0 || selected >= (int8_t)cat->count) return;

    if (g_parameter_style_index < 0) {
        for (uint8_t i = 0; i < cat->count; i++) {
            update_parameter_row(list, (int8_t)i, i == (uint8_t)selected);
        }
    } else if (g_parameter_style_index != selected) {
        update_parameter_row(list, g_parameter_style_index, 0);
        update_parameter_row(list, selected, 1);
    } else {
        update_parameter_row(list, selected, 1);
    }
    g_parameter_style_index = selected;

    lv_obj_t *selected_row = lv_obj_get_child(list, selected);
    if (selected_row) lv_obj_scroll_to_view(selected_row, LV_ANIM_OFF);
}

static void update_parameter_row(lv_obj_t *list, int8_t row_idx, uint8_t selected)
{
    const set_category_t *cat = &categories[g_category_index];
    if (list == NULL || row_idx < 0 || row_idx >= (int8_t)cat->count) return;

    lv_obj_t *row = lv_obj_get_child(list, row_idx);
    if (row == NULL) return;

    lv_obj_t *name_lbl = lv_obj_get_child(row, 0);
    lv_obj_t *val_lbl = lv_obj_get_child(row, 2);
    lv_obj_t *unit_lbl = (lv_obj_get_child_cnt(row) > 3) ? lv_obj_get_child(row, 3) : NULL;
    if (name_lbl == NULL || val_lbl == NULL) return;

    if (selected) {
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ROW_SEL), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(COLOR_TEXT_SEL), 0);
        lv_obj_set_style_text_color(val_lbl, lv_color_hex(COLOR_TEXT_SEL), 0);
        if (unit_lbl) lv_obj_set_style_text_color(unit_lbl, lv_color_hex(COLOR_TEXT_SEL), 0);
    } else {
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(COLOR_TEXT_NORMAL), 0);
        lv_obj_set_style_text_color(val_lbl, lv_color_hex(COLOR_TEXT_NORMAL), 0);
        if (unit_lbl) lv_obj_set_style_text_color(unit_lbl, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    }
}

/**
 * @brief  参数列表按键处理 (二级菜单)
 *
 * UP/DOWN:  修改选中索引，队列异步高亮更新
 * OK:       队列异步创建编辑页面
 * SHIFT:    队列异步返回分类列表
 */
static void handle_parameter_key(uint8_t button_id)
{
    const set_category_t *cat = &categories[g_category_index];

    switch (button_id) {
    case BUTTON_ID_UP:
        if (g_set_nav.selected_index > 0) {
            g_set_nav.selected_index--;
            set_select_context_t *ctx = lv_malloc(sizeof(set_select_context_t));
            if (ctx) { ctx->new_index = g_set_nav.selected_index; lv_async_call(async_select_parameter_cb, ctx); }
        }
        break;
    case BUTTON_ID_DOWN:
        if (g_set_nav.selected_index < (int8_t)(cat->count - 1)) {
            g_set_nav.selected_index++;
            set_select_context_t *ctx = lv_malloc(sizeof(set_select_context_t));
            if (ctx) { ctx->new_index = g_set_nav.selected_index; lv_async_call(async_select_parameter_cb, ctx); }
        }
        break;
    case BUTTON_ID_OK:
        {
            set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
            if (ctx) {
                ctx->target_level = 0; ctx->category_idx = g_category_index; ctx->item_idx = g_set_nav.selected_index;
                g_set_busy = 1;
                lv_async_call(async_enter_edit_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_SHIFT:
        {
            set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
            if (ctx) {
                ctx->target_level = 0; ctx->category_idx = g_category_index; ctx->item_idx = 0;
                g_set_busy = 1;
                lv_async_call(async_enter_category_cb, ctx);
            }
        }
        break;
    default:
        break;
    }
}

/*============================================================================*/
/*                          编辑页面 - 全屏编辑                                */
/*                                                                            */
/* 全屏编辑单个参数。显示范围、当前值和大字编辑值。                            */
/* UP/DOWN 按步进调节，OK 保存到 EEPROM，SHIFT 取消。                          */
/* OK 和 SHIFT 都返回参数列表。                                               */
/*============================================================================*/

/**
 * @brief  创建编辑页面屏幕 (三级)
 *
 * 屏幕布局 (320x240):
 *   +------------------------------------------+
 *   | < Range Max              SHIFT:Back       |  顶栏 36px
 *   +------------------------------------------+
 *   |                                          |
 *   |        Range: 0 ~ 99999  mm             |  范围提示
 *   |        Current: 5000                     |  当前值 (只读)
 *   |                                          |
 *   |             5000                        |  编辑值 (48号字体)
 *   |              mm                         |  单位 (强调色)
 *   |                                          |
 *   +------------------------------------------+
 *   | UP/DOWN:Adjust  OK:Save  SHIFT:Cancel   |  底栏 30px
 *   +------------------------------------------+
 *
 * @param  cat_idx: 分类索引
 * @param  item_idx: 分类内的参数索引
 */
static lv_obj_t *create_edit_screen(uint8_t cat_idx, uint8_t item_idx)
{
    const set_item_t *item = &categories[cat_idx].items[item_idx];

    g_edit_item = item;

    /* 根据参数最大值生成步进列表，初始使用最小步进 */
    if (is_total_flow_item(item)) {
        /* double 路径: 累计流量专用步进 */
        g_step_count = 0;
        double s = 0.001;
        while (s <= 100000000000.0 && g_step_count < 15) {
            g_stepd_list[g_step_count++] = s;
            s *= 10.0;
        }
    } else if (item->getf) {
        /* float 路径: 根据 max_valf 动态生成步进列表 */
        g_step_count = 0;
        float s = 0.001f;
        while (s <= item->max_valf && g_step_count < 13) {
            g_stepf_list[g_step_count++] = s;
            s *= 10.0f;
        }
    } else {
        generate_step_list(item->max_val);
    }
    g_step_index = 0;

    lv_obj_t *screen = lv_obj_create(NULL);
    if (screen == NULL) return NULL;
    ui_container_style_init(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_size(screen, LV_HOR_RES, LV_VER_RES);

    /* --- 顶栏 --- */
    lv_obj_t *top_bar = lv_obj_create(screen);
    if (top_bar == NULL) { lv_obj_del(screen); return NULL; }
    ui_container_style_init(top_bar);
    lv_obj_set_size(top_bar, LV_HOR_RES, 36);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(top_bar, 10, 0);

    lv_obj_t *title_label = lv_label_create(top_bar);
    char title_buf[32];
    snprintf(title_buf, sizeof(title_buf), LV_SYMBOL_LEFT "  %s", lang_get(item->name_id));
    lv_label_set_text(title_label, title_buf);
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(title_label, lang_get_font_20(), 0);

    /* 编辑页 SHIFT 用于切换步进，不显示 "SHIFT:Back"，底栏已有操作提示 */

    /* --- 居中内容区 (范围 + 数值) --- */
    lv_obj_t *content = lv_obj_create(screen);
    if (content == NULL) { lv_obj_del(screen); return NULL; }
    ui_container_style_init(content);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 36 - 30);
    lv_obj_set_pos(content, 0, 36);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 范围提示 (显示允许的最小值 ~ 最大值) */
    lv_obj_t *range_label = lv_label_create(content);
    char range_buf[48];
    char min_buf[24], max_buf[24];
    if (is_total_flow_item(item)) {
        uint32_t sp = app_config_get_sum_point();
        snprintf(min_buf, sizeof(min_buf), "%.*f", (int)sp, 0.0);
        snprintf(max_buf, sizeof(max_buf), "%.*f", (int)sp, 999999999999.999);
        snprintf(range_buf, sizeof(range_buf), "Range: %s ~ %s", min_buf, max_buf);
    } else if (item->getf) {
        snprintf(min_buf, sizeof(min_buf), "%.*f", (int)item->f_decimals, item->min_valf);
        snprintf(max_buf, sizeof(max_buf), "%.*f", (int)item->f_decimals, item->max_valf);
        snprintf(range_buf, sizeof(range_buf), "Range: %s ~ %s", min_buf, max_buf);
    } else if (item->decimal_places > 0 && !item->format) {
        format_with_decimal(item->min_val, item->decimal_places, min_buf, sizeof(min_buf));
        format_with_decimal(item->max_val, item->decimal_places, max_buf, sizeof(max_buf));
        snprintf(range_buf, sizeof(range_buf), "Range: %s ~ %s", min_buf, max_buf);
    } else {
        snprintf(range_buf, sizeof(range_buf), "Range: %lu ~ %lu",
                 (unsigned long)item->min_val, (unsigned long)get_effective_max(item));
    }
    lv_label_set_text(range_label, range_buf);
    lv_obj_set_style_text_color(range_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(range_label, lang_get_font_16(), 0);
    lv_obj_set_style_margin_bottom(range_label, 15, 0);

    /* 当前已保存的值 (只读显示) */
    lv_obj_t *cur_label = lv_label_create(content);
    lv_label_set_text(cur_label, "Current:");
    lv_obj_set_style_text_color(cur_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(cur_label, lang_get_font_16(), 0);

    lv_obj_t *cur_val_label = lv_label_create(content);
    if (item->format) {
        lv_label_set_text(cur_val_label, item->format(item->get()));
    } else if (item->getf) {
        char cur_val_buf[16];
        snprintf(cur_val_buf, sizeof(cur_val_buf), "%.*f",
                 (int)item->f_decimals, item->getf());
        lv_label_set_text(cur_val_label, cur_val_buf);
    } else if (item->decimal_places > 0) {
        char cur_val_buf[16];
        format_with_decimal(item->get(), item->decimal_places,
                           cur_val_buf, sizeof(cur_val_buf));
        lv_label_set_text(cur_val_label, cur_val_buf);
    } else {
        char cur_val_buf[16];
        snprintf(cur_val_buf, sizeof(cur_val_buf), "%lu", (unsigned long)item->get());
        lv_label_set_text(cur_val_label, cur_val_buf);
    }
    lv_obj_set_style_text_color(cur_val_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(cur_val_label, lang_get_font_18(), 0);
    lv_obj_set_style_margin_bottom(cur_val_label, 20, 0);

    /* 可编辑的值 (大字体，通过 UP/DOWN 经异步回调更新) */
    g_edit_value_label = lv_label_create(content);
    if (is_total_flow_item(item)) {
        g_set_nav.edit_valued = flow_calc_get_total();  /* double 路径 */
    } else if (item->getf) {
        g_set_nav.edit_valuef = item->getf();  /* float 路径 */
    } else {
        g_set_nav.edit_value = item->get();    /* uint32 路径 */
    }
    /* 校准参数: 进入编辑时立即将PWM输出设为当前值 */
    g_is_cal_edit = is_calibration_item(item);
    if (g_is_cal_edit) {
        app_current_set_calibration(g_set_nav.edit_value);
    }
    lv_label_set_recolor(g_edit_value_label, true); /* 启用 recolor 以高亮步进位 */
    /* 字体和颜色选择 */
    if (is_total_flow_item(item)) {
        /* 累计流量 (double, 大范围) 用 26px，与首页一致 */
        lv_obj_set_style_text_color(g_edit_value_label, lv_color_hex(COLOR_TEXT_SEL), 0);
        lv_obj_set_style_text_font(g_edit_value_label, &lv_font_montserrat_26, 0);
    } else if (item->format && !item->getf) {
        /* 纯 format 类型 (如水渠类型、语言) 用强调色 + 24px */
        lv_obj_set_style_text_color(g_edit_value_label, lv_color_hex(COLOR_STEP_HL), 0);
        lv_obj_set_style_text_font(g_edit_value_label, lang_get_font_24(), 0);
    } else {
        /* 普通 uint32 / float 用 48px 大字 */
        lv_obj_set_style_text_color(g_edit_value_label, lv_color_hex(COLOR_TEXT_SEL), 0);
        lv_obj_set_style_text_font(g_edit_value_label, &lv_font_montserrat_48, 0);
    }
    lv_obj_set_style_margin_bottom(g_edit_value_label, 5, 0);
    update_edit_value_display();  /* 初始显示带步进位高亮 */

    /* 单位标签 */
    lv_obj_t *unit_label = lv_label_create(content);
    if (item->unit && item->unit[0] != '\0') {
        lv_label_set_text(unit_label, item->unit);
    } else {
        lv_label_set_text(unit_label, "");
    }
    lv_obj_set_style_text_color(unit_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(unit_label, lang_get_font_16(), 0);

    /* --- 底栏 (操作提示) --- */
    lv_obj_t *bottom_bar = lv_obj_create(screen);
    ui_container_style_init(bottom_bar);
    lv_obj_set_size(bottom_bar, LV_HOR_RES, 30);
    lv_obj_set_pos(bottom_bar, 0, LV_VER_RES - 30);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(COLOR_BOTTOM_BG), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(bottom_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bottom_bar, 10, 0);
    lv_obj_set_style_pad_right(bottom_bar, 10, 0);

    lv_obj_t *h_ok = lv_label_create(bottom_bar);
    lv_label_set_text(h_ok, "OK:Save");
    lv_obj_set_style_text_color(h_ok, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h_ok, lang_get_font_14(), 0);

    lv_obj_t *h_up = lv_label_create(bottom_bar);
    lv_label_set_text(h_up, "UP");
    lv_obj_set_style_text_color(h_up, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h_up, lang_get_font_14(), 0);

    lv_obj_t *h_down = lv_label_create(bottom_bar);
    lv_label_set_text(h_down, "DOWN");
    lv_obj_set_style_text_color(h_down, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h_down, lang_get_font_14(), 0);

    lv_obj_t *h_shift = lv_label_create(bottom_bar);
    lv_label_set_text(h_shift, "SHIFT:Step");
    lv_obj_set_style_text_color(h_shift, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h_shift, lang_get_font_14(), 0);

    return screen;
}

/**
 * @brief  更新编辑页面的值显示 (带步进位高亮)
 *
 * 读取 g_set_nav.edit_value(f) 并格式化到 g_edit_value_label。
 * 使用 LVGL recolor 功能在步进对应的位插入高亮颜色标记。
 *
 * 例如: 值=5000, 步进=10 → "50#2effde 0#0" (十位高亮)
 *       值=123, 步进=100 → "1#2effde 2#3" (百位高亮)
 *       值=5000, 步进=1, decimal=3 → "5.00#2effde 0#" (0.001位高亮)
 *
 * @note  必须在 g_edit_value_label 上先调用 lv_label_set_recolor(true)
 */
static void update_edit_value_display(void)
{
    if (g_edit_value_label == NULL) return;

    /* 有 format 回调且无 getf 时直接显示文本，不做数字高亮 */
    if (g_edit_item && g_edit_item->format && !g_edit_item->getf) {
        lv_label_set_text(g_edit_value_label, g_edit_item->format(g_set_nav.edit_value));
        return;
    }

    /* ---------- double 路径 (累计流量): 按位高亮 ---------- */
    if (g_edit_item && is_total_flow_item(g_edit_item)) {
        uint32_t dp = app_config_get_sum_point();
        double dval = g_set_nav.edit_valued;
        double multiplier = 1.0;
        for (uint8_t i = 0; i < dp; i++) multiplier *= 10.0;

        uint64_t ivalue = (uint64_t)(dval * multiplier + 0.5);
        uint64_t istep = (uint64_t)(g_stepd_list[g_step_index] * multiplier + 0.5);
        int pos_from_right = 0;
        while (istep > 1) { pos_from_right++; istep /= 10; }

        char val_str[24];
        snprintf(val_str, sizeof(val_str), "%llu", (unsigned long long)ivalue);
        int len = (int)strlen(val_str);

        int max_int_digits = 0;
        {
            uint64_t imax = (uint64_t)(g_edit_item->max_valf);
            while (imax > 0) { max_int_digits++; imax /= 10; }
        }
        if (max_int_digits < 1) max_int_digits = 1;
        int min_len = max_int_digits + (int)dp;
        if (len < min_len) {
            int pad = min_len - len;
            memmove(&val_str[pad], val_str, len + 1);
            memset(val_str, '0', pad);
            len = min_len;
        }

        int int_digits = len - (int)dp;
        int digit_pos = len - 1 - pos_from_right;
        if (digit_pos < 0 || digit_pos >= len) {
            char buf[40];
            int bi = 0;
            for (int i = 0; i < len; i++) {
                if (dp > 0 && i == int_digits) buf[bi++] = '.';
                buf[bi++] = val_str[i];
            }
            buf[bi] = '\0';
            lv_label_set_text(g_edit_value_label, buf);
            return;
        }

        char buf[48];
        int bi = 0;
        for (int i = 0; i < len; i++) {
            if (dp > 0 && i == int_digits) buf[bi++] = '.';
            if (i == digit_pos) {
                bi += snprintf(buf + bi, sizeof(buf) - bi, "#%06x %c#",
                               (unsigned int)COLOR_STEP_HL, val_str[i]);
            } else {
                buf[bi++] = val_str[i];
            }
        }
        buf[bi] = '\0';
        lv_label_set_text(g_edit_value_label, buf);
        return;
    }

    /* ---------- float 路径 ---------- */
    if (g_edit_item && g_edit_item->getf) {
        float fval = g_set_nav.edit_valuef;
        uint8_t dp = g_edit_item->f_decimals;

        /* 大范围参数 (如累计流量): 缩放后超出 int32 范围，跳过步进高亮 */
        float multiplier = 1.0f;
        for (uint8_t i = 0; i < dp; i++) multiplier *= 10.0f;
        if (fval * multiplier > 2000000000.0f) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%.*f", (int)dp, fval);
            lv_label_set_text(g_edit_value_label, buf);
            return;
        }

        int32_t ivalue = (int32_t)(fval * multiplier + (fval >= 0 ? 0.5f : -0.5f));
        if (ivalue < 0) ivalue = 0;

        /* 步进位: stepf * multiplier 得到整数的步进位 */
        float fstep = g_stepf_list[g_step_index];
        int32_t istep = (int32_t)(fstep * multiplier + 0.5f);

        int pos_from_right = 0;
        {
            int32_t s = istep;
            while (s > 1) { pos_from_right++; s /= 10; }
        }

        char val_str[16];
        snprintf(val_str, sizeof(val_str), "%ld", (long)ivalue);
        int len = (int)strlen(val_str);

        /*
         * 前补零保证宽度: 根据 max_valf 的整数位数 + dp位小数。
         * 例如 max_valf=99999, dp=3 → 整数最大5位 → 最少 5+3=8 位
         */
        int max_int_digits = 0;
        {
            int32_t imax = (int32_t)(g_edit_item->max_valf);
            while (imax > 0) { max_int_digits++; imax /= 10; }
        }
        if (max_int_digits < 1) max_int_digits = 1;
        int min_len = max_int_digits + dp;
        if (len < min_len) {
            int pad = min_len - len;
            memmove(&val_str[pad], val_str, len + 1);
            memset(val_str, '0', pad);
            len = min_len;
        }

        int int_digits = len - dp;
        int digit_pos = len - 1 - pos_from_right;
        if (digit_pos < 0 || digit_pos >= len) {
            /* 超范围直接显示 */
            char buf[32];
            int bi = 0;
            for (int i = 0; i < len; i++) {
                if (i == int_digits) buf[bi++] = '.';
                buf[bi++] = val_str[i];
            }
            if (int_digits == len) buf[bi++] = '.';
            buf[bi] = '\0';
            lv_label_set_text(g_edit_value_label, buf);
            return;
        }

        char buf[32];
        int bi = 0;
        for (int i = 0; i < len; i++) {
            if (i == int_digits) buf[bi++] = '.';
            if (i == digit_pos) {
                bi += snprintf(buf + bi, sizeof(buf) - bi, "#%06x %c#",
                               (unsigned int)COLOR_STEP_HL, val_str[i]);
            } else {
                buf[bi++] = val_str[i];
            }
        }
        if (int_digits == len) buf[bi++] = '.';
        buf[bi] = '\0';
        lv_label_set_text(g_edit_value_label, buf);
        return;
    }

    /* ---------- uint32 路径 (原有逻辑) ---------- */
    uint32_t value = g_set_nav.edit_value;
    uint32_t step = g_step_list[g_step_index];
    uint8_t dp = (g_edit_item && !g_edit_item->format) ? g_edit_item->decimal_places : 0;

    /* 根据步进值计算需要高亮的位: step=1→第0位(个位), step=10→第1位(十位), ... */
    int pos_from_right = 0;
    {
        uint32_t s = step;
        while (s > 1) { pos_from_right++; s /= 10; }
    }

    /* 将当前值转为纯数字字符串 */
    char val_str[16];
    snprintf(val_str, sizeof(val_str), "%lu", (unsigned long)value);
    int len = (int)strlen(val_str);

    /*
     * 统一按最大步进的位数前补零，保证切换步进时显示宽度不变。
     * 例如: max_step=10000(5位), 值=50 → 显示 "00050"
     */
    int max_digits = 0;
    {
        uint32_t s = g_step_list[g_step_count - 1];
        while (s > 0) { max_digits++; s /= 10; }
    }
    int min_len = max_digits;
    if (len < min_len) {
        int pad = min_len - len;
        memmove(&val_str[pad], val_str, len + 1);
        memset(val_str, '0', pad);
        len = min_len;
    }

    /* 纯数字中的高亮位 (从左起, 0-indexed) */
    int digit_pos = len - 1 - pos_from_right;

    if (dp > 0) {
        /* ---------- 有小数: 在数字串中插入小数点 ---------- */
        int int_digits = len - dp;
        if (int_digits < 1) int_digits = 1;
        /* 保证整数部分至少1位: 在前面补零 */
        while (int_digits + dp > len) {
            memmove(&val_str[1], val_str, len + 1);
            val_str[0] = '0';
            len++;
        }
        int_digits = len - dp;
        /* digit_pos 超出范围则不插入小数 */
        if (digit_pos < 0 || digit_pos >= len) {
            lv_label_set_text(g_edit_value_label, val_str);
            return;
        }
        /*
         * 计算高亮位在插入小数点后的字符串中的索引。
         * 小数点插在 int_digits 之后。
         * digit_pos < int_digits → 不跨过小数点
         * digit_pos >= int_digits → 跨过小数点
         */

        /* 构建带小数点和高亮的字符串 */
        char buf[32];
        int bi = 0;
        for (int i = 0; i < len; i++) {
            if (i == int_digits) {
                buf[bi++] = '.';
            }
            if (i == digit_pos) {
                bi += snprintf(buf + bi, sizeof(buf) - bi, "#%06x %c#",
                               (unsigned int)COLOR_STEP_HL, val_str[i]);
            } else {
                buf[bi++] = val_str[i];
            }
        }
        /* 如果 int_digits == len，小数点追加在末尾 */
        if (int_digits == len) {
            buf[bi++] = '.';
        }
        buf[bi] = '\0';
        lv_label_set_text(g_edit_value_label, buf);
    } else {
        /* ---------- 无小数: 原有逻辑 ---------- */
        int pos_from_left = len - 1 - pos_from_right;
        if (pos_from_left >= 0 && pos_from_left < len) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.*s#%06x %c#%s",
                     pos_from_left, val_str,
                     (unsigned int)COLOR_STEP_HL,
                     val_str[pos_from_left],
                     &val_str[pos_from_left + 1]);
            lv_label_set_text(g_edit_value_label, buf);
        } else {
            lv_label_set_text(g_edit_value_label, val_str);
        }
    }
}

/**
 * @brief  根据参数最大值生成步进列表
 *
 * 生成 1, 10, 100, 1000, 10000 中不超过 max_val 的步进值。
 * 例如: max_val=20 → [1, 10], max_val=99999 → [1,10,100,1000,10000]
 *
 * @param  max_val: 参数最大值
 * @retval 生成的步进级数 (1~5)
 */
static uint8_t generate_step_list(uint32_t max_val)
{
    g_step_count = 0;
    uint32_t step = 1;
    while (step <= max_val && g_step_count < 5) {
        g_step_list[g_step_count++] = step;
        step *= 10;
    }
    return g_step_count;
}

/**
 * @brief  编辑页面按键处理 (三级)
 *
 * UP:    增加 edit_value (不超过 max_val)
 * DOWN:  减少 edit_value (不低于 min_val)
 * OK:    保存 edit_value 到配置 + EEPROM，返回参数列表
 * SHIFT: 放弃 edit_value，返回参数列表
 *
 * OK 和 SHIFT 都导航回参数列表 (重新创建屏幕以刷新显示的值)。
 */
static void handle_edit_key(uint8_t button_id, uint8_t event)
{
    const set_item_t *item = &categories[g_category_index].items[g_set_nav.selected_index];

    switch (button_id) {
    case BUTTON_ID_UP:
        if (is_total_flow_item(item)) {
            double step = g_stepd_list[g_step_index];
            double new_val = g_set_nav.edit_valued + step;
            if (new_val > (double)item->max_valf) new_val = (double)item->max_valf;
            g_set_nav.edit_valued = new_val;
            set_edit_val_context_t *ctxd = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctxd) { ctxd->new_valued = new_val; lv_async_call(async_update_edit_val_cb, ctxd); }
        } else if (item->getf) {
            float step = g_stepf_list[g_step_index];
            float new_val = g_set_nav.edit_valuef + step;
            if (new_val > item->max_valf) new_val = item->max_valf;
            g_set_nav.edit_valuef = new_val;
            set_edit_val_context_t *ctxf = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctxf) { ctxf->new_valuef = new_val; lv_async_call(async_update_edit_val_cb, ctxf); }
        } else if (item->format) {
            /* format类型: 循环设置 */
            if (g_set_nav.edit_value >= get_effective_max(item)) {
                g_set_nav.edit_value = item->min_val;
            } else {
                g_set_nav.edit_value += g_step_list[g_step_index];
            }
            set_edit_val_context_t *ctxi = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctxi) { ctxi->new_value = g_set_nav.edit_value; lv_async_call(async_update_edit_val_cb, ctxi); }
        } else {
            if (g_set_nav.edit_value + g_step_list[g_step_index] <= item->max_val) {
                g_set_nav.edit_value += g_step_list[g_step_index];
            } else {
                g_set_nav.edit_value = item->max_val;
            }
            if (g_is_cal_edit) app_current_set_calibration(g_set_nav.edit_value);
            set_edit_val_context_t *ctxi = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctxi) { ctxi->new_value = g_set_nav.edit_value; lv_async_call(async_update_edit_val_cb, ctxi); }
        }
        break;
    case BUTTON_ID_DOWN:
        if (is_total_flow_item(item)) {
            double step = g_stepd_list[g_step_index];
            double new_val = g_set_nav.edit_valued - step;
            if (new_val < 0.0) new_val = 0.0;
            g_set_nav.edit_valued = new_val;
            set_edit_val_context_t *ctxd = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctxd) { ctxd->new_valued = new_val; lv_async_call(async_update_edit_val_cb, ctxd); }
        } else if (item->getf) {
            float step = g_stepf_list[g_step_index];
            float new_val = g_set_nav.edit_valuef - step;
            if (new_val < item->min_valf) new_val = item->min_valf;
            g_set_nav.edit_valuef = new_val;
            set_edit_val_context_t *ctxf = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctxf) { ctxf->new_valuef = new_val; lv_async_call(async_update_edit_val_cb, ctxf); }
        } else if (item->format) {
            /* format类型: 循环设置 */
            if (g_set_nav.edit_value <= item->min_val) {
                g_set_nav.edit_value = get_effective_max(item);
            } else {
                g_set_nav.edit_value -= g_step_list[g_step_index];
            }
            set_edit_val_context_t *ctxi = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctxi) { ctxi->new_value = g_set_nav.edit_value; lv_async_call(async_update_edit_val_cb, ctxi); }
        } else {
            if (g_set_nav.edit_value >= item->min_val + g_step_list[g_step_index]) {
                g_set_nav.edit_value -= g_step_list[g_step_index];
            } else {
                g_set_nav.edit_value = item->min_val;
            }
            if (g_is_cal_edit) app_current_set_calibration(g_set_nav.edit_value);
            set_edit_val_context_t *ctxi = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctxi) { ctxi->new_value = g_set_nav.edit_value; lv_async_call(async_update_edit_val_cb, ctxi); }
        }
        break;
    case BUTTON_ID_OK:
        if (is_clear_sd_item(item) && g_set_nav.edit_value == 1) {
            item->set(g_set_nav.edit_value);
            g_set_busy = 1;
            lv_async_call(async_start_clear_sd_cb, NULL);
            break;
        }

        /* 保存: 写入配置结构体 + 持久化到 EEPROM */
        if (is_total_flow_item(item)) {
            flow_calc_set_total(g_set_nav.edit_valued);
        } else if (item->setf) {
            item->setf(g_set_nav.edit_valuef);
        } else {
            item->set(g_set_nav.edit_value);
        }
        /* 保存由 app_config_set/setf 内部标记 dirty, app_config_process() 延迟写入 EEPROM */
        if (g_is_cal_edit) { g_is_cal_edit = 0; app_current_exit_calibration(); }
        g_edit_value_label = NULL;  /* 防止屏幕删除后的悬空指针 */
        g_edit_item = NULL;
        /* 返回参数列表 (重新创建屏幕以显示更新后的值) */
        {
            set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
            if (ctx) {
                ctx->target_level = 0; ctx->category_idx = g_category_index; ctx->item_idx = g_set_nav.selected_index;
                g_set_busy = 1;
                lv_async_call(async_enter_parameter_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_SHIFT:
        if (event == BUTTON_EVENT_LONG) {
            /* 长按: 放弃编辑，返回参数列表 */
            if (g_is_cal_edit) { g_is_cal_edit = 0; app_current_exit_calibration(); }
            g_edit_value_label = NULL;
            g_edit_item = NULL;
            {
                set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
                if (ctx) {
                    ctx->target_level = 0; ctx->category_idx = g_category_index; ctx->item_idx = g_set_nav.selected_index;
                    g_set_busy = 1;
                    lv_async_call(async_enter_parameter_cb, ctx);
                }
            }
        } else {
            /* 短按: 循环切换步进值 */
            if (g_step_count > 1) {
                g_step_index = (g_step_index + 1) % g_step_count;
                set_step_context_t *ctx = lv_malloc(sizeof(set_step_context_t));
                if (ctx) { ctx->new_step_index = g_step_index; lv_async_call(async_update_step_cb, ctx); }
            }
        }
        break;
    default:
        break;
    }
}

/*============================================================================*/
/*                          屏幕切换辅助函数                                  */
/*============================================================================*/

/**
 * @brief  带智能自动删除的屏幕加载
 *
 * 与 ui_switch_screen() 总是自动删除旧屏幕不同，
 * 本函数检查旧屏幕是否为主屏幕。如果是则保留 (auto_del=false)，
 * 防止进入设置时主屏幕被释放。其他设置子屏幕则自动释放以节省 RAM。
 */
static void set_screen_load(lv_obj_t *new_screen, lv_screen_load_anim_t anim, uint32_t time)
{
    uint8_t auto_del = (ui_manager->active_screen != ui_manager->main_screen);
    lv_screen_load_anim(new_screen, anim, time, 0, auto_del);
    ui_manager->active_screen = new_screen;
}

/**
 * @brief  空闲超时回调 (LVGL定时器，运行在LVGL上下文)
 *
 * 每秒检查一次，若距上次按键超过 15 秒则自动返回主屏幕。
 */
static void idle_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    if (lv_tick_get() - g_last_key_tick >= IDLE_TIMEOUT_MS) {
        if (g_idle_timer) {
            lv_timer_del(g_idle_timer);
            g_idle_timer = NULL;
        }
        if (g_clear_sd_timer) {
            lv_timer_del(g_clear_sd_timer);
            g_clear_sd_timer = NULL;
        }
        g_edit_value_label = NULL;
        g_edit_item = NULL;
        if (g_is_cal_edit) { g_is_cal_edit = 0; app_current_exit_calibration(); }
        ui_manager->settings_screen = NULL;
        g_set_nav.level = SET_LEVEL_CATEGORY;
        set_screen_load(ui_manager->main_screen, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, ANIM_TIME);
        g_set_busy = 0;
    }
}

static void update_clear_sd_progress_display(int8_t progress)
{
    if (g_edit_value_label == NULL) return;

    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)progress);
    lv_label_set_recolor(g_edit_value_label, false);
    lv_obj_set_style_text_color(g_edit_value_label, lv_color_hex(COLOR_TEXT_SEL), 0);
    lv_obj_set_style_text_font(g_edit_value_label, &lv_font_montserrat_48, 0);
    lv_label_set_text(g_edit_value_label, buf);
}

static void clear_sd_progress_timer_cb(lv_timer_t *timer)
{
    int8_t progress = app_log_get_clear_sd_progress();

    (void)timer;
    g_last_key_tick = lv_tick_get();
    update_clear_sd_progress_display(progress);

    if (progress >= 100) {
        if (g_clear_sd_timer) {
            lv_timer_del(g_clear_sd_timer);
            g_clear_sd_timer = NULL;
        }

        g_edit_value_label = NULL;
        g_edit_item = NULL;

        set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
        if (ctx) {
            ctx->target_level = 0;
            ctx->category_idx = g_category_index;
            ctx->item_idx = g_set_nav.selected_index;
            g_set_busy = 1;
            lv_async_call(async_enter_parameter_cb, ctx);
        } else {
            g_set_busy = 0;
        }
    }
}

static void async_start_clear_sd_cb(void *context)
{
    (void)context;

    g_last_key_tick = lv_tick_get();
    update_clear_sd_progress_display(0);

    if (g_clear_sd_timer) {
        lv_timer_del(g_clear_sd_timer);
        g_clear_sd_timer = NULL;
    }

    g_clear_sd_timer = lv_timer_create(clear_sd_progress_timer_cb, 200, NULL);
    if (g_clear_sd_timer == NULL) {
        g_set_busy = 0;
    }
}

/*============================================================================*/
/*                          异步回调函数 (LVGL上下文)                         */
/*                                                                            */
/* 这些回调在 lv_timer_handler() 内部的 main_task 中执行。                    */
/* 可以安全地创建/修改/销毁 LVGL 对象。                                        */
/* 每个回调在使用完上下文后会 lv_free() 释放。                                  */
/*============================================================================*/

/**
 * @brief  异步: 进入分类列表 (一级菜单)
 *
 * 创建新的分类屏幕并以左滑动画加载。
 * 调用场景: 从主屏幕进入设置，或在参数列表中按 SHIFT 返回。
 */
static void async_enter_category_cb(void *context)
{
    set_nav_context_t *ctx = (set_nav_context_t *)context;

    lv_obj_t *screen = create_category_screen();
    if (screen == NULL) {
        g_set_busy = 0;
        lv_free(ctx);
        return;
    }

    g_set_nav.level = SET_LEVEL_CATEGORY;
    g_set_nav.selected_index = ctx->category_idx;
    g_set_nav.current_screen = screen;
    ui_manager->settings_screen = screen;
    update_category_selection();

    set_screen_load(screen, LV_SCREEN_LOAD_ANIM_FADE_IN, ANIM_TIME);
    g_set_busy = 0;

    lv_free(ctx);
}

/**
 * @brief  异步: 进入参数列表 (二级菜单)
 *
 * 为指定分类创建新的参数屏幕。
 * 调用场景: 在分类列表中按 OK，或在编辑页面中按 OK/SHIFT。
 */
static void async_enter_parameter_cb(void *context)
{
    set_nav_context_t *ctx = (set_nav_context_t *)context;

    lv_obj_t *screen = create_parameter_screen(ctx->category_idx);
    if (screen == NULL) {
        g_set_busy = 0;
        lv_free(ctx);
        return;
    }

    g_set_nav.level = SET_LEVEL_PARAMETER;
    g_set_nav.selected_index = ctx->item_idx;
    g_set_nav.current_screen = screen;
    ui_manager->settings_screen = screen;
    update_parameter_selection();

    set_screen_load(screen, LV_SCREEN_LOAD_ANIM_FADE_IN, ANIM_TIME);
    g_set_busy = 0;

    lv_free(ctx);
}

/**
 * @brief  异步: 进入编辑页面 (三级)
 *
 * 为指定参数创建新的编辑屏幕。
 * 调用场景: 在参数列表中按 OK。
 */
static void async_enter_edit_cb(void *context)
{
    set_nav_context_t *ctx = (set_nav_context_t *)context;

    lv_obj_t *screen = create_edit_screen(ctx->category_idx, ctx->item_idx);
    if (screen == NULL) {
        g_set_busy = 0;
        lv_free(ctx);
        return;
    }

    g_set_nav.level = SET_LEVEL_EDITING;
    g_set_nav.selected_index = ctx->item_idx;
    g_set_nav.current_screen = screen;
    ui_manager->settings_screen = screen;

    set_screen_load(screen, LV_SCREEN_LOAD_ANIM_FADE_IN, ANIM_TIME);
    g_set_busy = 0;

    lv_free(ctx);
}

/**
 * @brief  异步: 退出到主屏幕
 *
 * 以右滑动画加载主屏幕。当前设置屏幕在动画完成后自动释放。
 * 清除 settings_screen 指针，使按键事件路由到主屏幕处理函数。
 */
static void async_exit_to_main_cb(void *context)
{
    (void)context;
    if (g_clear_sd_timer) {
        lv_timer_del(g_clear_sd_timer);
        g_clear_sd_timer = NULL;
    }
    g_edit_value_label = NULL;
    g_edit_item = NULL;
    ui_manager->settings_screen = NULL;
    g_set_nav.level = SET_LEVEL_CATEGORY;
    set_screen_load(ui_manager->main_screen, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, ANIM_TIME);
    g_set_busy = 0;
}

/**
 * @brief  异步: 更新分类列表选中高亮
 *
 * 从上下文读取新索引并应用高亮样式。
 * 调用场景: 分类列表中的 UP/DOWN 按键。
 */
static void async_select_category_cb(void *context)
{
    set_select_context_t *ctx = (set_select_context_t *)context;
    g_set_nav.selected_index = ctx->new_index;
    update_category_selection();
    lv_free(ctx);
}

/**
 * @brief  异步: 更新参数列表选中高亮
 *
 * 从上下文读取新索引并应用高亮样式。
 * 调用场景: 参数列表中的 UP/DOWN 按键。
 */
static void async_select_parameter_cb(void *context)
{
    set_select_context_t *ctx = (set_select_context_t *)context;
    g_set_nav.selected_index = ctx->new_index;
    update_parameter_selection();
    lv_free(ctx);
}

/**
 * @brief  异步: 更新编辑值显示
 *
 * 从上下文读取新值并更新大字标签。
 * 调用场景: 编辑页面中的 UP/DOWN 按键。
 */
static void async_update_edit_val_cb(void *context)
{
    set_edit_val_context_t *ctx = (set_edit_val_context_t *)context;
    if (g_edit_item && is_total_flow_item(g_edit_item)) {
        g_set_nav.edit_valued = ctx->new_valued;
    } else if (g_edit_item && g_edit_item->getf) {
        g_set_nav.edit_valuef = ctx->new_valuef;
    } else {
        g_set_nav.edit_value = ctx->new_value;
    }
    update_edit_value_display();
    lv_free(ctx);
}

/**
 * @brief  异步: 更新编辑页面的步进高亮
 *
 * 从上下文读取新的步进索引，重新渲染值标签以更新高亮位。
 * 调用场景: 编辑页面中按 SHIFT 切换步进。
 */
static void async_update_step_cb(void *context)
{
    set_step_context_t *ctx = (set_step_context_t *)context;
    g_step_index = ctx->new_step_index;
    update_edit_value_display();  /* 重新渲染带新步进高亮的值 */
    lv_free(ctx);
}
