/**
 * @file ui_history.c
 * @brief 历史记录查询界面实现
 *
 * 两个子页面:
 *   1. 日期时间选择页 - 选择年/月/日/时
 *   2. 数据浏览页 - 显示瞬时流量和累计流量，每条记录两行
 *
 * 进入方式: 主页面长按SHIFT
 * 退出方式: 长按SHIFT / 15秒无操作
 *
 * 线程安全: 按键回调只修改状态变量，所有UI操作通过lv_async_call()执行。
 *           文件I/O在log_task中通过history_query_process()执行。
 */

#include "ui_history.h"
#include "ui.h"
#include "ui_conf.h"
#include "ui_lang.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include "../../Drivers/Button/button_driver.h"
#include "../../Drivers/File/data_recorder.h"
#include "rtc_time.h"
#include <stdio.h>
#include <string.h>

/*============================================================================*/
/*                            常量定义                                          */
/*============================================================================*/

#define ANIM_TIME               200         /**< 屏幕切换动画时长 (ms) */
#define IDLE_TIMEOUT_MS         15000       /**< 空闲超时时间 (15秒) */
#define IDLE_CHECK_PERIOD_MS    1000        /**< 空闲检测周期 (1秒) */
#define HIST_WINDOW_SIZE        21          /**< 滑动窗口大小 (21*33=693字节) */
#define HIST_VISIBLE_COUNT      3           /**< 可见记录数(上/当前/下) */
#define HIST_EDGE_MARGIN        2           /**< 预加载触发边缘 */
#define HIST_ANCHOR_INDEX       5           /**< 重新加载后当前项目标位置 */

#define YEAR_MIN    2024
#define YEAR_MAX    2030
#define HIST_FLOW_UNIT_STR      "m\xC2\xB3/h"

/*============================================================================*/
/*                            模块状态                                          */
/*============================================================================*/

typedef enum {
    HIST_STATE_IDLE,
    HIST_STATE_DATE_PICKER,
    HIST_STATE_LOADING,
    HIST_STATE_BROWSER,
} hist_state_t;

/* 日期编辑字段索引 */
enum {
    FIELD_YEAR = 0,
    FIELD_MONTH,
    FIELD_DAY,
    FIELD_HOUR,
    FIELD_COUNT
};

/* 滑动窗口缓存 */
typedef struct {
    DataRecord records[HIST_WINDOW_SIZE];
    int16_t collect_start;       /**< 窗口起始绝对索引 */
    int16_t window_center;       /**< 当前记录在窗口中的偏移 */
    uint16_t total_count;        /**< 该时段总记录数 */
    int16_t current_absolute;    /**< 当前记录的绝对索引 */
} hist_cache_t;

static volatile hist_state_t g_hist_state = HIST_STATE_IDLE;
static volatile uint8_t g_hist_busy;       /**< 过渡保护标志 */

/* 日期选择状态 */
static uint16_t g_hist_year;
static uint8_t  g_hist_month;
static uint8_t  g_hist_day;
static uint8_t  g_hist_hour;
static uint8_t  g_hist_field;              /**< 当前编辑字段 */

/* 数据缓存 */
static hist_cache_t g_hist_cache;
static volatile uint8_t g_hist_data_ready;  /**< 数据就绪标志 (volatile, 跨任务) */

/* 查询请求 (button task -> log_task) */
static volatile uint8_t g_hist_query_pending;
static volatile uint32_t g_hist_query_seq;
static volatile uint32_t g_hist_pending_query_seq;

/* LVGL控件指针 (仅LVGL上下文访问) */
static lv_obj_t *g_hist_screen;
static lv_obj_t *g_date_labels[FIELD_COUNT];
static lv_obj_t *g_record_labels[HIST_VISIBLE_COUNT * 4];  /**< 每条记录2行，每行左右2个Label */
static lv_obj_t *g_record_conts[HIST_VISIBLE_COUNT];       /**< 每条记录容器 */
static lv_obj_t *g_status_label;
static lv_obj_t *g_top_date_label;
static lv_obj_t *g_top_count_label;
static lv_obj_t *g_no_data_label;

static lv_timer_t *g_idle_timer;
static lv_timer_t *g_poll_timer;           /**< 数据就绪轮询定时器 */
static uint32_t g_last_key_tick;
static uint32_t g_enter_tick;           /**< 进入时刻 (用于屏蔽松手重复LONG事件) */
static uint32_t g_exit_tick;            /**< 退出时刻 (用于屏蔽退出后松手重入) */

#define ENTER_PROTECT_MS  2500          /**< 进入后屏蔽LONG退出的时间 */
#define EXIT_PROTECT_MS   2500          /**< 退出后屏蔽松手重入的时间 */
#define POLL_PERIOD_MS    200           /**< 数据就绪检测周期 */

/*============================================================================*/
/*                          前向声明                                            */
/*============================================================================*/

/* 屏幕创建 */
static lv_obj_t *create_date_picker_screen(void);
static lv_obj_t *create_browser_screen(void);

/* 显示更新 (LVGL上下文) */
static void update_date_picker_display(void);
static void update_browser_display(void);

/* 异步回调 */
static void async_enter_date_picker_cb(void *ctx);
static void async_update_date_cb(void *ctx);
static void async_enter_browser_cb(void *ctx);
static void async_update_browser_cb(void *ctx);
static void async_exit_to_main_cb(void *ctx);

/* 辅助 */
static void hist_screen_load(lv_obj_t *new_screen, lv_screen_load_anim_t anim, uint32_t time);
static void history_start_timers(void);
static void history_stop_timers(void);
static void history_clear_widget_refs(void);
static void history_request_query(void);
static void history_cancel_query(void);
static uint8_t history_take_query(uint32_t *query_seq);
static uint8_t history_query_is_current(uint32_t query_seq);
static uint8_t history_commit_query_result(uint32_t query_seq, const hist_cache_t *cache);
static int16_t history_calc_collect_start(int16_t current_absolute,
                                          int16_t previous_collect_start,
                                          uint16_t previous_total_count);
static void reset_idle_timer(void);
static void idle_timeout_cb(lv_timer_t *timer);
static void poll_timer_cb(lv_timer_t *timer);
static uint8_t get_days_in_month(uint16_t year, uint8_t month);
static void clamp_day(void);

/*============================================================================*/
/*                          公共API                                             */
/*============================================================================*/

void history_screen_enter(void)
{
    if (g_hist_state != HIST_STATE_IDLE) return;
    if (lv_tick_get() - g_exit_tick < EXIT_PROTECT_MS) return;

    g_hist_busy = 0;

    /* 默认值: 当前RTC时间 */
    RTC_TimeData rtc;
    RTC_Time_Get(&rtc);
    g_hist_year  = rtc.year;
    g_hist_month = rtc.month;
    g_hist_day   = rtc.date;
    g_hist_hour  = rtc.hour;
    g_hist_field = FIELD_HOUR;  /* 默认高亮"时" */

    g_hist_state = HIST_STATE_DATE_PICKER;
    g_enter_tick = lv_tick_get();
    g_hist_query_pending = 0;
    g_hist_data_ready = 0;

    g_hist_busy = 1;
    lv_async_call(async_enter_date_picker_cb, NULL);
}

void history_screen_exit(void)
{
    g_exit_tick = lv_tick_get();
    g_hist_busy = 1;
    g_hist_state = HIST_STATE_IDLE;
    history_cancel_query();
    memset(&g_hist_cache, 0, sizeof(g_hist_cache));
    history_clear_widget_refs();

    lv_async_call(async_exit_to_main_cb, NULL);
}

void history_button_handler(uint8_t button_id, uint8_t event)
{
    if (g_hist_busy) return;
    g_last_key_tick = lv_tick_get();

    /* 长按SHIFT: 退出 (屏蔽进入后松手产生的重复LONG事件) */
    if (event == BUTTON_EVENT_LONG && button_id == BUTTON_ID_SHIFT) {
        if (lv_tick_get() - g_enter_tick < ENTER_PROTECT_MS) return;
        history_screen_exit();
        return;
    }

    if (g_hist_state == HIST_STATE_DATE_PICKER) {
        /* 只处理短按 */
        if (event != BUTTON_EVENT_SHORT) return;

        switch (button_id) {
        case BUTTON_ID_UP:
            /* 增加当前字段 */
            switch (g_hist_field) {
            case FIELD_YEAR:
                if (g_hist_year < YEAR_MAX) g_hist_year++;
                else g_hist_year = YEAR_MIN;
                break;
            case FIELD_MONTH:
                if (g_hist_month < 12) g_hist_month++;
                else g_hist_month = 1;
                break;
            case FIELD_DAY:
                if (g_hist_day < get_days_in_month(g_hist_year, g_hist_month))
                    g_hist_day++;
                else g_hist_day = 1;
                break;
            case FIELD_HOUR:
                if (g_hist_hour < 23) g_hist_hour++;
                else g_hist_hour = 0;
                break;
            }
            clamp_day();
            g_hist_busy = 1;
            lv_async_call(async_update_date_cb, NULL);
            break;

        case BUTTON_ID_DOWN:
            /* 减少当前字段 */
            switch (g_hist_field) {
            case FIELD_YEAR:
                if (g_hist_year > YEAR_MIN) g_hist_year--;
                else g_hist_year = YEAR_MAX;
                break;
            case FIELD_MONTH:
                if (g_hist_month > 1) g_hist_month--;
                else g_hist_month = 12;
                break;
            case FIELD_DAY:
                if (g_hist_day > 1) g_hist_day--;
                else g_hist_day = get_days_in_month(g_hist_year, g_hist_month);
                break;
            case FIELD_HOUR:
                if (g_hist_hour > 0) g_hist_hour--;
                else g_hist_hour = 23;
                break;
            }
            clamp_day();
            g_hist_busy = 1;
            lv_async_call(async_update_date_cb, NULL);
            break;

        case BUTTON_ID_SHIFT:
            /* 切换编辑字段 */
            g_hist_field = (g_hist_field + 1) % FIELD_COUNT;
            g_hist_busy = 1;
            lv_async_call(async_update_date_cb, NULL);
            break;

        case BUTTON_ID_OK:
            /* 确认日期时间，触发查询 */
            g_hist_state = HIST_STATE_LOADING;
            memset(&g_hist_cache, 0, sizeof(g_hist_cache));
            g_hist_cache.current_absolute = 0;
            history_request_query();

            g_hist_busy = 1;
            lv_async_call(async_enter_browser_cb, NULL);
            break;

        default:
            break;
        }
    }
    else if (g_hist_state == HIST_STATE_BROWSER) {
        /* 只处理短按 */
        if (event != BUTTON_EVENT_SHORT) return;

        switch (button_id) {
        case BUTTON_ID_UP:
            if (g_hist_cache.total_count == 0) break;
            if (g_hist_cache.current_absolute > 0) {
                g_hist_cache.current_absolute--;
                /* 窗口前沿有未加载数据时才触发预加载 */
                if (g_hist_cache.collect_start > 0 &&
                    g_hist_cache.current_absolute <= g_hist_cache.collect_start + HIST_EDGE_MARGIN) {
                    g_hist_state = HIST_STATE_LOADING;
                    history_request_query();
                }
                g_hist_busy = 1;
                lv_async_call(async_update_browser_cb, NULL);
            }
            break;

        case BUTTON_ID_DOWN:
            if (g_hist_cache.total_count == 0) break;
            if (g_hist_cache.current_absolute < (int16_t)(g_hist_cache.total_count - 1)) {
                g_hist_cache.current_absolute++;
                /* 窗口后沿有未加载数据时才触发预加载 */
                if (g_hist_cache.collect_start + HIST_WINDOW_SIZE < (int16_t)g_hist_cache.total_count &&
                    g_hist_cache.current_absolute >=
                        g_hist_cache.collect_start + HIST_WINDOW_SIZE - 1 - HIST_EDGE_MARGIN) {
                    g_hist_state = HIST_STATE_LOADING;
                    history_request_query();
                }
                g_hist_busy = 1;
                lv_async_call(async_update_browser_cb, NULL);
            }
            break;

        case BUTTON_ID_OK:
            /* 返回日期选择页 */
            g_hist_state = HIST_STATE_DATE_PICKER;
            g_hist_busy = 1;
            lv_async_call(async_enter_date_picker_cb, NULL);
            break;

        case BUTTON_ID_SHIFT:
            /* 返回日期选择页 */
            g_hist_state = HIST_STATE_DATE_PICKER;
            g_hist_busy = 1;
            lv_async_call(async_enter_date_picker_cb, NULL);
            break;

        default:
            break;
        }
    }
}

/*============================================================================*/
/*                    log_task 调用的查询处理                                    */
/*============================================================================*/

typedef struct {
    hist_cache_t *cache;
    uint32_t scan_index;
} hist_query_ctx_t;

static void hist_query_cb(const DataRecord *record, void *user_data)
{
    hist_query_ctx_t *ctx = (hist_query_ctx_t *)user_data;
    hist_cache_t *cache = ctx->cache;
    uint32_t record_index = ctx->scan_index++;

    if (record_index >= (uint32_t)cache->collect_start &&
        record_index < (uint32_t)(cache->collect_start + HIST_WINDOW_SIZE)) {
        uint8_t idx = (uint8_t)(record_index - cache->collect_start);
        cache->records[idx] = *record;
    }

    cache->total_count++;
}

void history_query_process(void)
{
    uint32_t query_seq = 0;
    if (!history_take_query(&query_seq)) return;
    if (!history_query_is_current(query_seq)) return;

    uint16_t query_year = g_hist_year;
    uint8_t query_month = g_hist_month;
    uint8_t query_day = g_hist_day;
    uint8_t query_hour = g_hist_hour;
    int16_t query_current = g_hist_cache.current_absolute;
    int16_t query_collect_start = g_hist_cache.collect_start;
    uint16_t query_total_count = g_hist_cache.total_count;

    printf("[HIST] query: %04u-%02u-%02u %02u:00~%02u:59\r\n",
           query_year, query_month, query_day, query_hour, query_hour);

    /* 构建查询过滤: 选定日期的选定小时范围 */
    DataQueryFilter filter;
    memset(&filter, 0, sizeof(filter));
    filter.start_year  = query_year;
    filter.start_month = query_month;
    filter.start_day   = query_day;
    filter.start_hour  = query_hour;
    filter.start_min   = 0;
    filter.end_year    = query_year;
    filter.end_month   = query_month;
    filter.end_day     = query_day;
    filter.end_hour    = query_hour;
    filter.end_min     = 59;
    filter.filter_alarm_only = 0;

    hist_cache_t new_cache;
    memset(&new_cache, 0, sizeof(new_cache));
    new_cache.current_absolute = query_current;

    /* 单遍扫描: 同时计数并收集当前窗口 */
    int16_t center = query_current;
    if (center < 0) center = 0;
    new_cache.collect_start = history_calc_collect_start(center,
                                                         query_collect_start,
                                                         query_total_count);
    new_cache.window_center = center - new_cache.collect_start;
    new_cache.current_absolute = center;

    hist_query_ctx_t query_ctx = {
        .cache = &new_cache,
        .scan_index = 0,
    };
    data_query(&filter, hist_query_cb, &query_ctx);

    if (!history_query_is_current(query_seq)) {
        printf("[HIST] query canceled after scan\r\n");
        return;
    }

    printf("[HIST] total_count=%u\r\n", new_cache.total_count);

    if (new_cache.total_count == 0) {
        if (history_commit_query_result(query_seq, &new_cache)) {
            printf("[HIST] no data, state=BROWSER\r\n");
        } else {
            printf("[HIST] query canceled before no-data commit\r\n");
        }
        return;
    }

    /* 扫描后修正当前索引，窗口起点保持为本次扫描使用的位置 */
    center = new_cache.current_absolute;
    if (center >= (int16_t)new_cache.total_count)
        center = (int16_t)new_cache.total_count - 1;
    if (center < 0) center = 0;

    new_cache.window_center = center - new_cache.collect_start;
    new_cache.current_absolute = center;

    if (history_commit_query_result(query_seq, &new_cache)) {
        printf("[HIST] data ready, state=BROWSER\r\n");
    } else {
        printf("[HIST] query canceled before data commit\r\n");
    }
}

/*============================================================================*/
/*                          屏幕创建                                            */
/*============================================================================*/

/**
 * @brief  创建日期时间选择页
 *
 * 布局 (320x240):
 *   顶栏 28px: 标题 + SHIFT:返回
 *   内容区: 日期时间选择器居中
 *   底栏 24px: 操作提示
 */
static lv_obj_t *create_date_picker_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    if (!screen) return NULL;
    ui_container_style_init(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_size(screen, LV_HOR_RES, LV_VER_RES);

    /* --- 顶栏 --- */
    lv_obj_t *top_bar = lv_obj_create(screen);
    ui_container_style_init(top_bar);
    lv_obj_set_size(top_bar, LV_HOR_RES, 28);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(top_bar, 10, 0);

    lv_obj_t *title = lv_label_create(top_bar);
    lv_label_set_text(title, LV_SYMBOL_SHUFFLE "  ");
    lv_label_ins_text(title, strlen(LV_SYMBOL_SHUFFLE "  "), lang_get(LANG_HIST_TITLE));
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(title, lang_get_font_20(), 0);

    /* --- 内容区 --- */
    lv_obj_t *content = lv_obj_create(screen);
    ui_container_style_init(content);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 28 - 24);
    lv_obj_set_pos(content, 0, 28);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 日期时间显示行 */
    lv_obj_t *date_row = lv_obj_create(content);
    ui_container_style_init(date_row);
    lv_obj_set_size(date_row, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(date_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(date_row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(date_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(date_row, 2, 0);

    /* 年 */
    g_date_labels[FIELD_YEAR] = lv_label_create(date_row);
    lv_label_set_recolor(g_date_labels[FIELD_YEAR], true);
    lv_obj_set_style_text_font(g_date_labels[FIELD_YEAR], lang_get_font_24(), 0);

    lv_obj_t *sep1 = lv_label_create(date_row);
    lv_label_set_text(sep1, "-");
    lv_obj_set_style_text_color(sep1, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(sep1, lang_get_font_24(), 0);

    /* 月 */
    g_date_labels[FIELD_MONTH] = lv_label_create(date_row);
    lv_label_set_recolor(g_date_labels[FIELD_MONTH], true);
    lv_obj_set_style_text_font(g_date_labels[FIELD_MONTH], lang_get_font_24(), 0);

    lv_obj_t *sep2 = lv_label_create(date_row);
    lv_label_set_text(sep2, "-");
    lv_obj_set_style_text_color(sep2, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(sep2, lang_get_font_24(), 0);

    /* 日 */
    g_date_labels[FIELD_DAY] = lv_label_create(date_row);
    lv_label_set_recolor(g_date_labels[FIELD_DAY], true);
    lv_obj_set_style_text_font(g_date_labels[FIELD_DAY], lang_get_font_24(), 0);

    /* 空格分隔 */
    lv_obj_t *space = lv_label_create(date_row);
    lv_label_set_text(space, "  ");
    lv_obj_set_style_text_font(space, lang_get_font_24(), 0);

    /* 时 */
    g_date_labels[FIELD_HOUR] = lv_label_create(date_row);
    lv_label_set_recolor(g_date_labels[FIELD_HOUR], true);
    lv_obj_set_style_text_font(g_date_labels[FIELD_HOUR], lang_get_font_24(), 0);

    /* "时" 后缀 */
    lv_obj_t *hour_suffix = lv_label_create(date_row);
    lv_label_set_text(hour_suffix, lang_get(LANG_HIST_HOUR));
    lv_obj_set_style_text_color(hour_suffix, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(hour_suffix, lang_get_font_16(), 0);

    /* --- 底栏操作提示 --- */
    lv_obj_t *bottom_bar = lv_obj_create(screen);
    ui_container_style_init(bottom_bar);
    lv_obj_set_size(bottom_bar, LV_HOR_RES, 24);
    lv_obj_set_pos(bottom_bar, 0, LV_VER_RES - 24);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(COLOR_BOTTOM_BG), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(bottom_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *h1 = lv_label_create(bottom_bar);
    lv_label_set_text(h1, LV_SYMBOL_UP LV_SYMBOL_DOWN ":Adjust");
    lv_obj_set_style_text_color(h1, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h1, &lv_font_montserrat_12, 0);

    lv_obj_t *h2 = lv_label_create(bottom_bar);
    lv_label_set_text(h2, "SHIFT:Field");
    lv_obj_set_style_text_color(h2, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h2, &lv_font_montserrat_12, 0);

    lv_obj_t *h3 = lv_label_create(bottom_bar);
    lv_label_set_text(h3, "OK:Query");
    lv_obj_set_style_text_color(h3, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h3, &lv_font_montserrat_12, 0);

    update_date_picker_display();
    return screen;
}

/**
 * @brief  创建数据浏览页
 *
 * 布局 (320x240):
 *   顶栏 28px: 日期时间 + SHIFT:返回
 *   内容区: 3条记录卡片，每条两行(瞬时+累计)
 *   状态栏 22px: "记录 N/M"
 */
static lv_obj_t *create_browser_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    if (!screen) return NULL;
    ui_container_style_init(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_size(screen, LV_HOR_RES, LV_VER_RES);

    /* --- 顶栏 --- */
    lv_obj_t *top_bar = lv_obj_create(screen);
    ui_container_style_init(top_bar);
    lv_obj_set_size(top_bar, LV_HOR_RES, 28);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(top_bar, 10, 0);
    lv_obj_set_style_pad_right(top_bar, 10, 0);

    g_top_date_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(g_top_date_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(g_top_date_label, lang_get_font_16(), 0);

    lv_obj_t *top_spacer = lv_obj_create(top_bar);
    ui_container_style_init(top_spacer);
    lv_obj_set_style_bg_opa(top_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(top_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(top_spacer, 1);
    lv_obj_set_height(top_spacer, 1);

    g_top_count_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(g_top_count_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(g_top_count_label, &lv_font_montserrat_14, 0);

    /* --- 记录区域 --- */
    lv_obj_t *record_area = lv_obj_create(screen);
    ui_container_style_init(record_area);
    lv_obj_set_size(record_area, LV_HOR_RES, LV_VER_RES - 28 - 22);
    lv_obj_set_pos(record_area, 0, 28);
    lv_obj_set_style_bg_opa(record_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(record_area, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(record_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(record_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(record_area, 2, 0);

    for (int i = 0; i < HIST_VISIBLE_COUNT; i++) {
        g_record_conts[i] = lv_obj_create(record_area);
        ui_container_style_init(g_record_conts[i]);
        lv_obj_set_size(g_record_conts[i], LV_PCT(96), 58);
        lv_obj_set_style_pad_hor(g_record_conts[i], 10, 0);
        lv_obj_set_style_pad_ver(g_record_conts[i], 5, 0);
        lv_obj_set_style_bg_opa(g_record_conts[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(g_record_conts[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(g_record_conts[i], 4, 0);
        lv_obj_set_flex_flow(g_record_conts[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(g_record_conts[i], 2, 0);

        /* 左侧强调边框 (默认透明, 当前行显示) */
        lv_obj_set_style_border_side(g_record_conts[i], LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(g_record_conts[i], 0, 0);

        /* 第一行: 时间 + 瞬时流量 */
        for (int row = 0; row < 2; row++) {
            lv_obj_t *line = lv_obj_create(g_record_conts[i]);
            ui_container_style_init(line);
            lv_obj_set_size(line, LV_PCT(100), row == 0 ? 22 : 20);
            lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_opa(line, LV_OPA_TRANSP, 0);
            lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            int base = i * 4 + row * 2;

            g_record_labels[base] = lv_label_create(line);
            lv_obj_set_style_text_font(g_record_labels[base], lang_get_font_16(), 0);
            lv_obj_set_style_text_color(g_record_labels[base], lv_color_hex(COLOR_TEXT_NORMAL), 0);

            lv_obj_t *spacer = lv_obj_create(line);
            ui_container_style_init(spacer);
            lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_opa(spacer, LV_OPA_TRANSP, 0);
            lv_obj_set_flex_grow(spacer, 1);
            lv_obj_set_height(spacer, 1);

            g_record_labels[base + 1] = lv_label_create(line);
            lv_obj_set_style_text_font(g_record_labels[base + 1], lang_get_font_16(), 0);
            lv_obj_set_style_text_color(g_record_labels[base + 1], lv_color_hex(COLOR_TEXT_NORMAL), 0);
        }

        /* 第二行: 累计流量 */
    }

    /* --- 状态栏 --- */
    lv_obj_t *status_bar = lv_obj_create(screen);
    ui_container_style_init(status_bar);
    lv_obj_set_size(status_bar, LV_HOR_RES, 22);
    lv_obj_set_pos(status_bar, 0, LV_VER_RES - 22);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(COLOR_BOTTOM_BG), 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(status_bar, 8, 0);

    g_status_label = lv_label_create(status_bar);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(g_status_label, lang_get_font_16(), 0);
    lv_label_set_text(g_status_label, lang_get(LANG_HIST_LOADING));

    /* 右侧导航提示 */
    lv_obj_t *nav_hint = lv_label_create(status_bar);
    lv_obj_set_style_text_font(nav_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(nav_hint, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_label_set_text(nav_hint, LV_SYMBOL_UP LV_SYMBOL_DOWN " " LV_SYMBOL_SHUFFLE ":Back");

    update_browser_display();
    return screen;
}

/*============================================================================*/
/*                          显示更新 (LVGL上下文)                                */
/*============================================================================*/

static void update_date_picker_display(void)
{
    char buf[24];

    /* 年 */
    if (g_date_labels[FIELD_YEAR]) {
        if (g_hist_field == FIELD_YEAR) {
            snprintf(buf, sizeof(buf), "#%06x %04u#", 0xFF3333, g_hist_year);
        } else {
            snprintf(buf, sizeof(buf), "#%06x %04u#", 0xFFFFFF, g_hist_year);
        }
        lv_label_set_text(g_date_labels[FIELD_YEAR], buf);
    }

    /* 月 */
    if (g_date_labels[FIELD_MONTH]) {
        if (g_hist_field == FIELD_MONTH) {
            snprintf(buf, sizeof(buf), "#%06x %02u#", 0xFF3333, g_hist_month);
        } else {
            snprintf(buf, sizeof(buf), "#%06x %02u#", 0xFFFFFF, g_hist_month);
        }
        lv_label_set_text(g_date_labels[FIELD_MONTH], buf);
    }

    /* 日 */
    if (g_date_labels[FIELD_DAY]) {
        if (g_hist_field == FIELD_DAY) {
            snprintf(buf, sizeof(buf), "#%06x %02u#", 0xFF3333, g_hist_day);
        } else {
            snprintf(buf, sizeof(buf), "#%06x %02u#", 0xFFFFFF, g_hist_day);
        }
        lv_label_set_text(g_date_labels[FIELD_DAY], buf);
    }

    /* 时 */
    if (g_date_labels[FIELD_HOUR]) {
        if (g_hist_field == FIELD_HOUR) {
            snprintf(buf, sizeof(buf), "#%06x %02u#", 0xFF3333, g_hist_hour);
        } else {
            snprintf(buf, sizeof(buf), "#%06x %02u#", 0xFFFFFF, g_hist_hour);
        }
        lv_label_set_text(g_date_labels[FIELD_HOUR], buf);
    }
}

static void update_browser_display(void)
{
    /* 顶栏日期 */
    if (g_top_date_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u%s",
                 g_hist_year, g_hist_month, g_hist_day, g_hist_hour, lang_get(LANG_HIST_HOUR));
        lv_label_set_text(g_top_date_label, buf);
    }

    if (g_top_count_label) {
        char buf[20];
        if (g_hist_state == HIST_STATE_LOADING) {
            snprintf(buf, sizeof(buf), "--/--");
            lv_obj_set_style_text_color(g_top_count_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
        } else if (g_hist_cache.total_count == 0) {
            snprintf(buf, sizeof(buf), "0/0");
            lv_obj_set_style_text_color(g_top_count_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
        } else {
            snprintf(buf, sizeof(buf), "%u/%u",
                     g_hist_cache.current_absolute + 1,
                     g_hist_cache.total_count);
            lv_obj_set_style_text_color(g_top_count_label, lv_color_hex(COLOR_ACCENT), 0);
        }
        lv_label_set_text(g_top_count_label, buf);
    }

    /* 状态栏 */
    if (g_status_label) {
        if (g_hist_state == HIST_STATE_LOADING) {
            lv_label_set_text(g_status_label, lang_get(LANG_HIST_LOADING));
        } else if (g_hist_cache.total_count == 0) {
            lv_label_set_text(g_status_label, lang_get(LANG_HIST_NO_DATA));
        } else {
            lv_label_set_text(g_status_label, lang_get(LANG_HIST_RECORD));
        }
    }

    /* 记录行 */
    int16_t center = g_hist_cache.current_absolute;
    for (int i = 0; i < HIST_VISIBLE_COUNT; i++) {
        int16_t abs_idx = center + (i - HIST_VISIBLE_COUNT / 2); /* -1, 0, +1 */
        int win_idx = abs_idx - g_hist_cache.collect_start;
        int is_current = (i == HIST_VISIBLE_COUNT / 2);
        int base = i * 4;

        if (g_hist_cache.total_count > 0 &&
            abs_idx >= 0 &&
            abs_idx < (int16_t)g_hist_cache.total_count &&
            win_idx >= 0 && win_idx < HIST_WINDOW_SIZE) {

            DataRecord *rec = &g_hist_cache.records[win_idx];
            char time_buf[16], inst_buf[32], total_name[24], total_buf[32];

            /* 第一行: 时间 + 瞬时流量 (数值用强调色) */
            if (is_current) {
                snprintf(time_buf, sizeof(time_buf),
                         "%s %02u:%02u",
                         LV_SYMBOL_RIGHT,
                         rec->hour, rec->minute);
            } else {
                snprintf(time_buf, sizeof(time_buf),
                         "  %02u:%02u",
                         rec->hour, rec->minute);
            }
            snprintf(inst_buf, sizeof(inst_buf), "%.3f %s",
                     rec->instant_flow, HIST_FLOW_UNIT_STR);
            lv_label_set_text(g_record_labels[base], time_buf);
            lv_label_set_text(g_record_labels[base + 1], inst_buf);

            /* 第二行: 累计流量 (数值用强调色) */
            snprintf(total_name, sizeof(total_name), "%s",
                     lang_get(LANG_HIST_TOTAL_FLOW));
            snprintf(total_buf, sizeof(total_buf), "%.3f m\xC2\xB3",
                     rec->total_flow);
            lv_label_set_text(g_record_labels[base + 2], total_name);
            lv_label_set_text(g_record_labels[base + 3], total_buf);

            /* 高亮当前行: 左侧强调色边框 + 背景 */
            if (is_current) {
                lv_obj_set_style_bg_color(g_record_conts[i], lv_color_hex(COLOR_ROW_SEL), 0);
                lv_obj_set_style_bg_opa(g_record_conts[i], LV_OPA_COVER, 0);
                lv_obj_set_style_border_side(g_record_conts[i], LV_BORDER_SIDE_LEFT, 0);
                lv_obj_set_style_border_color(g_record_conts[i], lv_color_hex(COLOR_ACCENT), 0);
                lv_obj_set_style_border_opa(g_record_conts[i], LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(g_record_conts[i], 3, 0);
                lv_obj_set_style_text_color(g_record_labels[base], lv_color_hex(COLOR_TEXT_SEL), 0);
                lv_obj_set_style_text_color(g_record_labels[base + 1], lv_color_hex(COLOR_ACCENT), 0);
                lv_obj_set_style_text_color(g_record_labels[base + 2], lv_color_hex(COLOR_TEXT_SEL), 0);
                lv_obj_set_style_text_color(g_record_labels[base + 3], lv_color_hex(COLOR_ACCENT), 0);
            } else {
                lv_obj_set_style_bg_opa(g_record_conts[i], LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_side(g_record_conts[i], LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_color(g_record_conts[i], lv_color_hex(0x2A353B), 0);
                lv_obj_set_style_border_opa(g_record_conts[i], LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(g_record_conts[i], 1, 0);
                lv_obj_set_style_text_color(g_record_labels[base], lv_color_hex(COLOR_TEXT_NORMAL), 0);
                lv_obj_set_style_text_color(g_record_labels[base + 1], lv_color_hex(COLOR_TEXT_NORMAL), 0);
                lv_obj_set_style_text_color(g_record_labels[base + 2], lv_color_hex(COLOR_TEXT_NORMAL), 0);
                lv_obj_set_style_text_color(g_record_labels[base + 3], lv_color_hex(COLOR_TEXT_NORMAL), 0);
            }
        } else {
            for (int j = 0; j < 4; j++) {
                lv_label_set_text(g_record_labels[base + j], "");
                lv_obj_set_style_text_color(g_record_labels[base + j], lv_color_hex(COLOR_TEXT_NORMAL), 0);
            }
            lv_obj_set_style_bg_opa(g_record_conts[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_side(g_record_conts[i], LV_BORDER_SIDE_NONE, 0);
            lv_obj_set_style_border_opa(g_record_conts[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(g_record_conts[i], 0, 0);
        }
    }

    /* 无数据时在屏幕中央显示大字提示 */
    if (g_hist_cache.total_count == 0 && g_hist_state == HIST_STATE_BROWSER) {
        if (g_no_data_label == NULL) {
            g_no_data_label = lv_label_create(g_hist_screen ? g_hist_screen : lv_screen_active());
            lv_label_set_recolor(g_no_data_label, true);
            lv_obj_set_style_text_font(g_no_data_label, lang_get_font_24(), 0);
            lv_obj_set_style_text_color(g_no_data_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
            lv_obj_align(g_no_data_label, LV_ALIGN_CENTER, 0, 0);
        }
        lv_label_set_text(g_no_data_label, lang_get(LANG_HIST_NO_DATA));
    } else {
        if (g_no_data_label) {
            lv_obj_del(g_no_data_label);
            g_no_data_label = NULL;
        }
    }
}

/*============================================================================*/
/*                          屏幕切换辅助                                        */
/*============================================================================*/

static void hist_screen_load(lv_obj_t *new_screen, lv_screen_load_anim_t anim, uint32_t time)
{
    uint8_t auto_del = (ui_manager->active_screen != ui_manager->main_screen);
    lv_screen_load_anim(new_screen, anim, time, 0, auto_del);
    ui_manager->active_screen = new_screen;
}

static int16_t history_calc_collect_start(int16_t current_absolute,
                                          int16_t previous_collect_start,
                                          uint16_t previous_total_count)
{
    int16_t collect_start = 0;
    int16_t tail_anchor = HIST_WINDOW_SIZE - 1 - HIST_ANCHOR_INDEX;

    if (current_absolute < 0) current_absolute = 0;

    if (previous_total_count == 0) {
        collect_start = current_absolute - (HIST_WINDOW_SIZE / 2);
    } else if (previous_collect_start > 0 &&
               current_absolute <= previous_collect_start + HIST_EDGE_MARGIN) {
        collect_start = current_absolute - tail_anchor;
    } else if (previous_collect_start + HIST_WINDOW_SIZE < (int16_t)previous_total_count &&
               current_absolute >=
                   previous_collect_start + HIST_WINDOW_SIZE - 1 - HIST_EDGE_MARGIN) {
        collect_start = current_absolute - HIST_ANCHOR_INDEX;
    } else {
        collect_start = current_absolute - (HIST_WINDOW_SIZE / 2);
    }

    if (collect_start < 0) collect_start = 0;
    return collect_start;
}

static uint32_t history_next_query_seq(void)
{
    uint32_t next = g_hist_query_seq + 1;
    if (next == 0) next = 1;
    g_hist_query_seq = next;
    return next;
}

static void history_request_query(void)
{
    vTaskSuspendAll();
    g_hist_pending_query_seq = history_next_query_seq();
    g_hist_data_ready = 0;
    g_hist_query_pending = 1;
    (void)xTaskResumeAll();
}

static void history_cancel_query(void)
{
    vTaskSuspendAll();
    (void)history_next_query_seq();
    g_hist_query_pending = 0;
    g_hist_data_ready = 0;
    (void)xTaskResumeAll();
}

static uint8_t history_take_query(uint32_t *query_seq)
{
    uint8_t has_query = 0;

    vTaskSuspendAll();
    if (g_hist_query_pending) {
        if (query_seq) *query_seq = g_hist_pending_query_seq;
        g_hist_query_pending = 0;
        has_query = 1;
    }
    (void)xTaskResumeAll();

    return has_query;
}

static uint8_t history_query_is_current(uint32_t query_seq)
{
    uint8_t is_current;

    vTaskSuspendAll();
    is_current = (g_hist_state == HIST_STATE_LOADING &&
                  g_hist_query_seq == query_seq);
    (void)xTaskResumeAll();

    return is_current;
}

static uint8_t history_commit_query_result(uint32_t query_seq, const hist_cache_t *cache)
{
    uint8_t committed = 0;

    vTaskSuspendAll();
    if (g_hist_state == HIST_STATE_LOADING &&
        g_hist_query_seq == query_seq) {
        memcpy(&g_hist_cache, cache, sizeof(g_hist_cache));
        g_hist_data_ready = 1;
        g_hist_state = HIST_STATE_BROWSER;
        committed = 1;
    }
    (void)xTaskResumeAll();

    return committed;
}

static void history_clear_widget_refs(void)
{
    g_hist_screen = NULL;
    g_status_label = NULL;
    g_top_date_label = NULL;
    g_top_count_label = NULL;
    g_no_data_label = NULL;
    for (int i = 0; i < FIELD_COUNT; i++) g_date_labels[i] = NULL;
    for (int i = 0; i < HIST_VISIBLE_COUNT; i++) {
        g_record_conts[i] = NULL;
        for (int j = 0; j < 4; j++) {
            g_record_labels[i * 4 + j] = NULL;
        }
    }
}

static void history_start_timers(void)
{
    reset_idle_timer();

    if (g_poll_timer == NULL) {
        g_poll_timer = lv_timer_create(poll_timer_cb, POLL_PERIOD_MS, NULL);
    }
    lv_timer_set_period(g_poll_timer, POLL_PERIOD_MS);
    lv_timer_ready(g_poll_timer);
}

static void history_stop_timers(void)
{
    if (g_idle_timer) {
        lv_timer_del(g_idle_timer);
        g_idle_timer = NULL;
    }
    if (g_poll_timer) {
        lv_timer_del(g_poll_timer);
        g_poll_timer = NULL;
    }
}

static void reset_idle_timer(void)
{
    g_last_key_tick = lv_tick_get();
    if (g_idle_timer == NULL) {
        g_idle_timer = lv_timer_create(idle_timeout_cb, IDLE_CHECK_PERIOD_MS, NULL);
    }
    lv_timer_set_period(g_idle_timer, IDLE_CHECK_PERIOD_MS);
    lv_timer_ready(g_idle_timer);
}

/*============================================================================*/
/*                          异步回调 (LVGL上下文)                                */
/*============================================================================*/

static void async_enter_date_picker_cb(void *ctx)
{
    (void)ctx;
    if (g_hist_state == HIST_STATE_IDLE) return;

    history_start_timers();
    history_clear_widget_refs();
    lv_obj_t *screen = create_date_picker_screen();
    if (!screen) { g_hist_busy = 0; return; }

    g_hist_screen = screen;
    ui_manager->history_screen = screen;
    hist_screen_load(screen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, ANIM_TIME);
    g_hist_busy = 0;
}

static void async_update_date_cb(void *ctx)
{
    (void)ctx;
    if (g_hist_state == HIST_STATE_IDLE) return;

    update_date_picker_display();
    g_hist_busy = 0;
}

static void async_enter_browser_cb(void *ctx)
{
    (void)ctx;
    if (g_hist_state == HIST_STATE_IDLE) return;

    history_start_timers();
    history_clear_widget_refs();
    lv_obj_t *screen = create_browser_screen();
    if (!screen) { g_hist_busy = 0; return; }

    g_hist_screen = screen;
    ui_manager->history_screen = screen;
    hist_screen_load(screen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, ANIM_TIME);
    g_hist_busy = 0;
}

static void async_update_browser_cb(void *ctx)
{
    (void)ctx;
    if (g_hist_state == HIST_STATE_IDLE) return;

    update_browser_display();
    g_hist_busy = 0;
}

static void async_exit_to_main_cb(void *ctx)
{
    (void)ctx;
    history_stop_timers();
    history_clear_widget_refs();
    ui_manager->history_screen = NULL;
    hist_screen_load(ui_manager->main_screen, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, ANIM_TIME);
    g_hist_busy = 0;
}

/*============================================================================*/
/*                          数据就绪轮询 (LVGL定时器)                             */
/*============================================================================*/

/**
 * @brief 轮询定时器回调
 *
 * log_task 完成查询后只设置 data_ready 标志，
 * 此定时器在 LVGL 上下文中检测到标志后刷新显示。
 */
static void poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* 检测数据就绪 */
    if (g_hist_state == HIST_STATE_BROWSER && g_hist_data_ready) {
        g_hist_data_ready = 0;
        printf("[HIST] poll: updating display\r\n");
        update_browser_display();
        g_hist_busy = 0;
    }
}

/*============================================================================*/
/*                          空闲超时                                            */
/*============================================================================*/

static void idle_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    if (lv_tick_get() - g_last_key_tick >= IDLE_TIMEOUT_MS) {
        history_screen_exit();
    }
}

/*============================================================================*/
/*                          辅助函数                                            */
/*============================================================================*/

static uint8_t get_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        uint8_t leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    if (month >= 1 && month <= 12) return days[month];
    return 31;
}

static void clamp_day(void)
{
    uint8_t max_day = get_days_in_month(g_hist_year, g_hist_month);
    if (g_hist_day > max_day) g_hist_day = max_day;
}
