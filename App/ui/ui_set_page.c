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
#include "app_config.h"
#include "../../Drivers/Button/button_driver.h"
#include <stdio.h>
#include <string.h>

/*============================================================================*/
/*                              颜色定义                                       */
/*    沿用主屏幕的暗色主题                                                       */
/*============================================================================*/
#define COLOR_BG            0x1E272E    /* 屏幕背景 (深色)              */
#define COLOR_ROW_SEL       0x363636    /* 选中行高亮背景              */
#define COLOR_TEXT_SEL      0xFFFFFF    /* 选中项文字 (白色)            */
#define COLOR_TEXT_NORMAL   0xBDC3C7    /* 普通项文字 (浅灰)            */
#define COLOR_ACCENT        0x2effde    /* 强调色 (青绿色)              */
#define COLOR_STEP_HL       0xff3333    /* 步进位高亮 (红色)            */
#define COLOR_BOTTOM_BG     0x253035    /* 底栏背景                    */

#define ANIM_TIME           200         /* 屏幕切换动画时长 (ms)       */

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
    const char *name;           /**< 显示名称 (如 "Range Max")      */
    const char *unit;           /**< 单位字符串 (如 "mm"), 或 ""     */
    uint32_t (*get)(void);      /**< 从配置读取当前值                */
    void (*set)(uint32_t);      /**< 写入新值到配置                  */
    uint32_t min_val;           /**< 允许的最小值                    */
    uint32_t max_val;           /**< 允许的最大值                    */
    uint32_t step;              /**< 每次UP/DOWN的调节步进            */
} set_item_t;

/**
 * @brief 分类 - 一级菜单的一个条目
 *
 * 将相关的参数组织在一起，如"基本参数"、"报警参数"等。
 */
typedef struct {
    const char *name;           /**< 分类名称 (如 "Basic")          */
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
    uint32_t edit_value;        /**< 编辑中的临时值                  */
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

/* ---------- 基本参数 ---------- */
static const set_item_t basic_items[] = {
    {"Range Max",       "mm",  app_config_get_range_max,        app_config_set_range_max,        0, 99999, 100},
    {"Height",          "mm",  app_config_get_height,            app_config_set_height,           0, 99999, 100},
    {"4mA Cal",         "",    app_config_get_calibration_4ma,    app_config_set_calibration_4ma, 0, 99999, 1},
    {"20mA Cal",        "",    app_config_get_calibration_20ma,   app_config_set_calibration_20ma,0, 99999, 1},
    {"4mA Range",       "",    app_config_get_range_4ma,          app_config_set_range_4ma,        0, 99999, 1},
    {"20mA Range",      "",    app_config_get_range_20ma,         app_config_set_range_20ma,       0, 99999, 1},
    {"Decimal",         "",    app_config_get_point_num,          app_config_set_point_num,        0, 3,     1},
};

/* ---------- 测量参数 ---------- */
static const set_item_t measure_items[] = {
    {"Window Width",    "",     app_config_get_window_width,       app_config_set_window_width,      0, 1000,  1},
    {"Filter Count",    "",     app_config_get_filter_count,       app_config_set_filter_count,      0, 50,    1},
    {"Sample Delay",    "ms",   app_config_get_delay_time,         app_config_set_delay_time,        0, 1000,  10},
    {"Antenna Type",    "",     app_config_get_antenna_type,       app_config_set_antenna_type,      0, 10,    1},
    {"Blind Area",      "mm",   app_config_get_blind_area,         app_config_set_blind_area,        0, 1000,  10},
    {"Window Coeff",    "",     app_config_get_w_coeff,            app_config_set_w_coeff,           0, 10,    1},
    {"Measure Coeff",   "",     app_config_get_m_coeff,            app_config_set_m_coeff,           0, 10,    1},
};

/* ---------- Modbus从机参数 ---------- */
static const set_item_t modbus_items[] = {
    {"Slave Addr",      "",     app_config_get_modbus_addr,         app_config_set_modbus_addr,       0, 247,   1},
    {"Baud Rate",       "",     app_config_get_modbus_baudrate,     app_config_set_modbus_baudrate,   0, 16,    1},
    {"Stop Bits",       "",     app_config_get_modbus_stopbits,     app_config_set_modbus_stopbits,   0, 7,     1},
};

/* ---------- 报警参数 ---------- */
static const set_item_t alarm_items[] = {
    {"Alarm High",      "",     app_config_get_alarm_ah,            app_config_set_alarm_ah,           0, 9999999, 1},
    {"Alarm Low",       "",     app_config_get_alarm_al,            app_config_set_alarm_al,           0, 9999999, 1},
    {"Alarm High DB",   "",     app_config_get_alarm_dh,            app_config_set_alarm_dh,           0, 9999999, 1},
    {"Alarm Low DB",    "",     app_config_get_alarm_dl,            app_config_set_alarm_dl,           0, 9999999, 1},
    {"Alarm HiHi",      "",     app_config_get_alarm_aah,           app_config_set_alarm_aah,          0, 9999999, 1},
    {"Alarm LoLo",      "",     app_config_get_alarm_aal,           app_config_set_alarm_aal,          0, 9999999, 1},
};

/* ---------- 系统设置 ---------- */
static const set_item_t system_items[] = {
    {"Canal Type",      "",     app_config_get_canals_type,         app_config_set_canals_type,       1, 3,     1},
    {"Channel ID",      "",     app_config_get_channel_id,          app_config_set_channel_id,        1, 16,    1},
    {"Flow Unit",       "",     app_config_get_instant_unit,        app_config_set_instant_unit,      1, 8,     1},
    {"Sum Decimal",     "",     app_config_get_sum_point,           app_config_set_sum_point,          1, 3,     1},
    {"Dist Offset",     "mm",   app_config_get_dis_offset,          app_config_set_dis_offset,         0, 99999, 10},
    {"Language",        "",     app_config_get_language,            app_config_set_language,           0, 1,     1},
    {"Factory Reset",   "",     app_config_get_factory_settings,    app_config_set_factory_settings,  0, 1,     1},
};

/* ---------- 一级菜单分类表 ---------- */
static const set_category_t categories[] = {
    {"Basic",       basic_items,  sizeof(basic_items) / sizeof(basic_items[0])},
    {"Measure",     measure_items, sizeof(measure_items) / sizeof(measure_items[0])},
    {"Modbus",      modbus_items, sizeof(modbus_items) / sizeof(modbus_items[0])},
    {"Alarm",       alarm_items,  sizeof(alarm_items) / sizeof(alarm_items[0])},
    {"System",      system_items, sizeof(system_items) / sizeof(system_items[0])},
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
static uint32_t g_step_list[5];               /* 步进值列表 (1,10,100,1000,10000) */
static uint8_t g_step_count;                  /* 当前参数的步进级数 */
static uint8_t g_step_index;                  /* 当前选中的步进索引 */

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
    uint32_t new_value;         /**< 新的编辑值                      */
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

/* --- 按键处理函数 (仅限按键任务上下文调用，不含LVGL操作) --- */
static void handle_category_key(uint8_t button_id);
static void handle_parameter_key(uint8_t button_id);
static void handle_edit_key(uint8_t button_id);

/* --- 异步回调函数 (在LVGL上下文中执行) --- */
static void async_enter_category_cb(void *context);
static void async_enter_parameter_cb(void *context);
static void async_enter_edit_cb(void *context);
static void async_exit_to_main_cb(void *context);
static void async_select_category_cb(void *context);
static void async_select_parameter_cb(void *context);
static void async_update_edit_val_cb(void *context);
static void async_update_step_cb(void *context);

/* --- 屏幕切换辅助函数 --- */
static void set_screen_load(lv_obj_t *new_screen, lv_screen_load_anim_t anim, uint32_t time);

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
    g_set_nav.level = SET_LEVEL_CATEGORY;             /* 导航层级重置为一级菜单(分类列表) */
    g_set_nav.selected_index = 0;                     /* 选中项归零，默认高亮第一个分类 */
    g_set_nav.current_screen = NULL;                  /* 清空屏幕指针，防止指向已释放的旧屏幕 */

    set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t)); /* 分配异步回调所需的上下文数据 */
    if (ctx == NULL) return;                          /* 分配失败直接返回，此时busy=0不影响正常使用 */
    ctx->category_idx = 0;                            /* 目标分类索引: 0 = Basic */
    ctx->item_idx = 0;                                /* 目标参数索引: 0 = 第一个参数 */
    g_set_busy = 1;                                   /* 设置忙标志，屏蔽过渡动画期间的按键事件 */
    lv_async_call(async_enter_category_cb, ctx);      /* 投递到LVGL主任务执行，按键上下文不能直接操作UI */
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
    if (event != BUTTON_EVENT_SHORT) return;
    if (g_set_busy) return;   /* 过渡动画期间屏蔽按键 */

    switch (g_set_nav.level) {
    case SET_LEVEL_CATEGORY:
        handle_category_key(button_id);
        break;
    case SET_LEVEL_PARAMETER:
        handle_parameter_key(button_id);
        break;
    case SET_LEVEL_EDITING:
        handle_edit_key(button_id);
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
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);

    lv_obj_t *spacer = lv_obj_create(top_bar);
    ui_container_style_init(spacer);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_height(spacer, 1);

    lv_obj_t *back_label = lv_label_create(top_bar);
    lv_label_set_text(back_label, "SHIFT:Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, 0);
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
        lv_label_set_text(name_label, categories[i].name);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_18, 0);
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
        lv_obj_set_style_text_font(cnt, &lv_font_montserrat_14, 0);
        lv_obj_set_style_margin_right(cnt, 5, 0);
    }

    /* 应用初始选中高亮 */
    g_set_nav.selected_index = 0;
    update_category_selection();

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

    for (uint8_t i = 0; i < CATEGORY_COUNT; i++) {
        lv_obj_t *row = lv_obj_get_child(list, i);
        if (row == NULL) continue;

        /* 行子对象布局: [0]名称 [1]弹性空间 [2]箭头 [3]参数数量 */
        lv_obj_t *name_lbl = lv_obj_get_child(row, 0);
        lv_obj_t *arrow_lbl = lv_obj_get_child(row, 2);
        lv_obj_t *cnt_lbl = lv_obj_get_child(row, 3);

        if (i == (uint8_t)g_set_nav.selected_index) {
            /* 高亮选中行 */
            lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ROW_SEL), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(name_lbl, lv_color_hex(COLOR_TEXT_SEL), 0);
            lv_obj_set_style_text_color(arrow_lbl, lv_color_hex(COLOR_ACCENT), 0);
            if (cnt_lbl) lv_obj_set_style_text_color(cnt_lbl, lv_color_hex(COLOR_TEXT_SEL), 0);
        } else {
            /* 普通行 */
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(name_lbl, lv_color_hex(COLOR_TEXT_NORMAL), 0);
            lv_obj_set_style_text_color(arrow_lbl, lv_color_hex(COLOR_TEXT_NORMAL), 0);
            if (cnt_lbl) lv_obj_set_style_text_color(cnt_lbl, lv_color_hex(COLOR_TEXT_NORMAL), 0);
        }
    }

    /* 自动滚动到选中行可见位置 */
    lv_obj_scroll_to_view(lv_obj_get_child(list, g_set_nav.selected_index), LV_ANIM_ON);
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
            /* 将UI更新队列到LVGL上下文执行 */
            set_select_context_t *ctx = lv_malloc(sizeof(set_select_context_t));
            if (ctx) {
                ctx->new_index = g_set_nav.selected_index;
                lv_async_call(async_select_category_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_DOWN:
        if (g_set_nav.selected_index < (int8_t)(CATEGORY_COUNT - 1)) {
            g_set_nav.selected_index++;
            set_select_context_t *ctx = lv_malloc(sizeof(set_select_context_t));
            if (ctx) {
                ctx->new_index = g_set_nav.selected_index;
                lv_async_call(async_select_category_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_OK:
        /* 保存选中的分类索引 (跨层级保持) */
        g_category_index = g_set_nav.selected_index;
        {
            set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
            if (ctx) {
                ctx->category_idx = g_category_index;
                ctx->item_idx = 0;
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
    snprintf(title_buf, sizeof(title_buf), LV_SYMBOL_LEFT "  %s", cat->name);
    lv_label_set_text(title_label, title_buf);
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);

    lv_obj_t *spacer = lv_obj_create(top_bar);
    ui_container_style_init(spacer);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_height(spacer, 1);

    lv_obj_t *back_label = lv_label_create(top_bar);
    lv_label_set_text(back_label, "SHIFT:Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, 0);
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
    char val_buf[16];
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
        lv_label_set_text(name_label, item->name);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);

        lv_obj_t *spacer = lv_obj_create(row);
        ui_container_style_init(spacer);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_grow(spacer, 1);
        lv_obj_set_height(spacer, 1);

        /* 显示当前配置值 (从 app_config 内存副本读取) */
        lv_obj_t *val_label = lv_label_create(row);
        snprintf(val_buf, sizeof(val_buf), "%lu", (unsigned long)item->get());
        lv_label_set_text(val_label, val_buf);
        lv_obj_set_style_text_font(val_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(val_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);

        /* 单位标签 (仅当单位字符串非空时创建) */
        if (item->unit && item->unit[0] != '\0') {
            lv_obj_t *unit_label = lv_label_create(row);
            lv_label_set_text(unit_label, item->unit);
            lv_obj_set_style_text_font(unit_label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(unit_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
            lv_obj_set_style_margin_left(unit_label, 5, 0);
        }
    }

    /* 应用初始选中高亮 */
    g_set_nav.selected_index = 0;
    update_parameter_selection();

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

    for (uint8_t i = 0; i < cat->count; i++) {
        lv_obj_t *row = lv_obj_get_child(list, i);
        if (row == NULL) continue;

        lv_obj_t *name_lbl = lv_obj_get_child(row, 0);
        lv_obj_t *val_lbl = lv_obj_get_child(row, 2);
        lv_obj_t *unit_lbl = (lv_obj_get_child_cnt(row) > 3) ? lv_obj_get_child(row, 3) : NULL;

        if (i == (uint8_t)g_set_nav.selected_index) {
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

    int idx = g_set_nav.selected_index;
    if (idx >= 0 && idx < (int)cat->count) {
        lv_obj_scroll_to_view(lv_obj_get_child(list, idx), LV_ANIM_ON);
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
            if (ctx) {
                ctx->new_index = g_set_nav.selected_index;
                lv_async_call(async_select_parameter_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_DOWN:
        if (g_set_nav.selected_index < (int8_t)(cat->count - 1)) {
            g_set_nav.selected_index++;
            set_select_context_t *ctx = lv_malloc(sizeof(set_select_context_t));
            if (ctx) {
                ctx->new_index = g_set_nav.selected_index;
                lv_async_call(async_select_parameter_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_OK:
        {
            set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
            if (ctx) {
                ctx->category_idx = g_category_index;
                ctx->item_idx = g_set_nav.selected_index;
                g_set_busy = 1;
                lv_async_call(async_enter_edit_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_SHIFT:
        {
            set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
            if (ctx) {
                ctx->category_idx = g_category_index;
                ctx->item_idx = 0;
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

    /* 根据参数最大值生成步进列表，初始使用最小步进 */
    generate_step_list(item->max_val);
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
    snprintf(title_buf, sizeof(title_buf), LV_SYMBOL_LEFT "  %s", item->name);
    lv_label_set_text(title_label, title_buf);
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);

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
    char range_buf[32];
    snprintf(range_buf, sizeof(range_buf), "Range: %lu ~ %lu",
             (unsigned long)item->min_val, (unsigned long)item->max_val);
    lv_label_set_text(range_label, range_buf);
    lv_obj_set_style_text_color(range_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(range_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_margin_bottom(range_label, 15, 0);

    /* 当前已保存的值 (只读显示) */
    lv_obj_t *cur_label = lv_label_create(content);
    lv_label_set_text(cur_label, "Current:");
    lv_obj_set_style_text_color(cur_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(cur_label, &lv_font_montserrat_16, 0);

    lv_obj_t *cur_val_label = lv_label_create(content);
    char cur_val_buf[16];
    snprintf(cur_val_buf, sizeof(cur_val_buf), "%lu", (unsigned long)item->get());
    lv_label_set_text(cur_val_label, cur_val_buf);
    lv_obj_set_style_text_color(cur_val_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(cur_val_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_margin_bottom(cur_val_label, 20, 0);

    /* 可编辑的值 (大字体，通过 UP/DOWN 经异步回调更新) */
    g_edit_value_label = lv_label_create(content);
    g_set_nav.edit_value = item->get();  /* 从当前值开始编辑 */
    lv_label_set_recolor(g_edit_value_label, true); /* 启用 recolor 以高亮步进位 */
    lv_obj_set_style_text_color(g_edit_value_label, lv_color_hex(COLOR_TEXT_SEL), 0);
    lv_obj_set_style_text_font(g_edit_value_label, &lv_font_montserrat_48, 0);
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
    lv_obj_set_style_text_font(unit_label, &lv_font_montserrat_20, 0);

    /* --- 底栏 (操作提示) --- */
    lv_obj_t *bottom_bar = lv_obj_create(screen);
    ui_container_style_init(bottom_bar);
    lv_obj_set_size(bottom_bar, LV_HOR_RES, 30);
    lv_obj_set_pos(bottom_bar, 0, LV_VER_RES - 30);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(COLOR_BOTTOM_BG), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(bottom_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bottom_bar, 20, 0);

    lv_obj_t *hint_label = lv_label_create(bottom_bar);
    lv_label_set_text(hint_label, "UP/DOWN:Adjust  OK:Save  SHIFT:Step");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);

    return screen;
}

/**
 * @brief  更新编辑页面的值显示 (带步进位高亮)
 *
 * 读取 g_set_nav.edit_value 并格式化到 g_edit_value_label。
 * 使用 LVGL recolor 功能在步进对应的位插入高亮颜色标记。
 *
 * 例如: 值=5000, 步进=10 → "50#2effde 0#0" (十位高亮)
 *       值=123, 步进=100 → "1#2effde 2#3" (百位高亮)
 *
 * @note  必须在 g_edit_value_label 上先调用 lv_label_set_recolor(true)
 */
static void update_edit_value_display(void)
{
    if (g_edit_value_label == NULL) return;

    uint32_t value = g_set_nav.edit_value;
    uint32_t step = g_step_list[g_step_index];

    /* 将当前值转为字符串 */
    char val_str[16];
    snprintf(val_str, sizeof(val_str), "%lu", (unsigned long)value);
    int len = (int)strlen(val_str);

    /* 根据步进值计算需要高亮的位: step=1→第0位(个位), step=10→第1位(十位), ... */
    int pos_from_right = 0;
    {
        uint32_t s = step;
        while (s > 1) { pos_from_right++; s /= 10; }
    }
    int pos_from_left = len - 1 - pos_from_right;

    if (pos_from_left >= 0 && pos_from_left < len) {
        /* 在目标位前后插入 recolor 标记: #RRGGBB digit# */
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*s#%06x %c#%s",
                 pos_from_left, val_str,
                 (unsigned int)COLOR_STEP_HL,
                 val_str[pos_from_left],
                 &val_str[pos_from_left + 1]);
        lv_label_set_text(g_edit_value_label, buf);
    } else {
        /* 步进位超出当前值位数，不高亮 */
        lv_label_set_text(g_edit_value_label, val_str);
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
static void handle_edit_key(uint8_t button_id)
{
    const set_item_t *item = &categories[g_category_index].items[g_set_nav.selected_index];

    switch (button_id) {
    case BUTTON_ID_UP:
        if (g_set_nav.edit_value + g_step_list[g_step_index] <= item->max_val) {
            g_set_nav.edit_value += g_step_list[g_step_index];
        } else {
            g_set_nav.edit_value = item->max_val;
        }
        /* 队列显示更新到LVGL上下文 */
        {
            set_edit_val_context_t *ctx = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctx) {
                ctx->new_value = g_set_nav.edit_value;
                lv_async_call(async_update_edit_val_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_DOWN:
        if (g_set_nav.edit_value >= item->min_val + g_step_list[g_step_index]) {
            g_set_nav.edit_value -= g_step_list[g_step_index];
        } else {
            g_set_nav.edit_value = item->min_val;
        }
        {
            set_edit_val_context_t *ctx = lv_malloc(sizeof(set_edit_val_context_t));
            if (ctx) {
                ctx->new_value = g_set_nav.edit_value;
                lv_async_call(async_update_edit_val_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_OK:
        /* 保存: 写入配置结构体 + 持久化到 EEPROM */
        item->set(g_set_nav.edit_value);
        app_config_save();
        g_edit_value_label = NULL;  /* 防止屏幕删除后的悬空指针 */
        /* 返回参数列表 (重新创建屏幕以显示更新后的值) */
        {
            set_nav_context_t *ctx = lv_malloc(sizeof(set_nav_context_t));
            if (ctx) {
                ctx->category_idx = g_category_index;
                ctx->item_idx = g_set_nav.selected_index;
                g_set_busy = 1;
                lv_async_call(async_enter_parameter_cb, ctx);
            }
        }
        break;
    case BUTTON_ID_SHIFT:
        /* 循环切换步进值 */
        if (g_step_count > 1) {
            g_step_index = (g_step_index + 1) % g_step_count;
            set_step_context_t *ctx = lv_malloc(sizeof(set_step_context_t));
            if (ctx) {
                ctx->new_step_index = g_step_index;
                lv_async_call(async_update_step_cb, ctx);
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

    set_screen_load(screen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, ANIM_TIME);
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

    set_screen_load(screen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, ANIM_TIME);
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

    set_screen_load(screen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, ANIM_TIME);
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
    g_edit_value_label = NULL;
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
    g_set_nav.edit_value = ctx->new_value;
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
