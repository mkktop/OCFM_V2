/**
 * @file ui_password.c
 * @brief 密码验证界面实现
 *
 * ============================================================================
 * 架构概览
 * ============================================================================
 *
 * 进入设置菜单前的密码锁屏界面。4位数字密码，输入方式与参数编辑页类似。
 *
 * 操作方式:
 *   UP/DOWN  - 调整当前位数字 (0~9循环)
 *   SHIFT    - 切换高亮位 (个→十→百→千循环)
 *   OK       - 确认密码，正确则进入设置，错误则重置
 *   SHIFT长按 - 取消，返回主屏幕
 *
 * 线程安全: 与ui_set_page.c相同的模式，按键回调只修改状态变量，
 *           所有UI操作通过lv_async_call()延迟到LVGL主任务执行。
 * ============================================================================
 */

#include "ui_password.h"
#include "ui.h"
#include "ui_conf.h"
#include "ui_set_page.h"
#include "lvgl.h"
#include "../../Drivers/Button/button_driver.h"
#include <stdio.h>
#include <string.h>

/*============================================================================*/
/*                            常量定义                                          */
/*============================================================================*/

#define PASSWORD_VALUE          1234        /**< 固定密码 */
#define PASSWORD_DIGITS         4           /**< 密码位数 */
#define ANIM_TIME               200         /**< 屏幕切换动画时长 (ms) */
#define IDLE_TIMEOUT_MS         15000       /**< 空闲超时时间 (15秒) */
#define IDLE_CHECK_PERIOD_MS    1000        /**< 空闲检测周期 (1秒) */
#define ERROR_DISPLAY_MS        1500        /**< 密码错误提示显示时长 (ms) */

/*============================================================================*/
/*                            模块状态                                          */
/*============================================================================*/

/** 步进表: 个位=1, 十位=10, 百位=100, 千位=1000 */
static const uint32_t g_step_list[PASSWORD_DIGITS] = {1, 10, 100, 1000};

static uint32_t g_input_value;              /**< 当前输入值 (0~9999) */
static uint8_t  g_step_index;               /**< 当前高亮位索引 (0=个位, 3=千位) */
static lv_obj_t *g_value_label;             /**< 数字显示标签指针 (异步回调用) */
static lv_obj_t *g_error_label;             /**< 错误提示标签指针 */
static volatile uint8_t g_password_busy;    /**< 过渡保护标志 */
static lv_timer_t *g_idle_timer;            /**< 空闲超时定时器 */
static uint32_t g_last_key_tick;            /**< 上次按键时间 */

/*============================================================================*/
/*                          前向声明                                            */
/*============================================================================*/

static void async_enter_password_cb(void *context);
static void async_password_correct_cb(void *context);
static void async_password_wrong_cb(void *context);
static void async_update_value_cb(void *context);
static void async_exit_to_main_cb(void *context);
static void idle_timeout_cb(lv_timer_t *timer);
static lv_obj_t *create_password_screen(void);
static void update_password_display(void);
static void password_screen_load(lv_obj_t *new_screen, lv_screen_load_anim_t anim, uint32_t time);

/*============================================================================*/
/*                          公共API                                             */
/*============================================================================*/

/**
 * @brief  进入密码验证界面
 *
 * 重置输入状态，异步创建密码屏幕。
 * 从主屏幕长按OK触发。
 */
void password_screen_enter(void)
{
    g_password_busy = 0;
    g_input_value = 0;
    g_step_index = PASSWORD_DIGITS - 1;  /* 从最左侧(千位)开始 */
    g_last_key_tick = lv_tick_get();

    /* 创建空闲超时定时器 */
    if (g_idle_timer == NULL) {
        g_idle_timer = lv_timer_create(idle_timeout_cb, IDLE_CHECK_PERIOD_MS, NULL);
    }
    lv_timer_set_period(g_idle_timer, IDLE_CHECK_PERIOD_MS);
    lv_timer_ready(g_idle_timer);

    g_password_busy = 1;
    lv_async_call(async_enter_password_cb, NULL);
}

/**
 * @brief  退出密码界面，返回主屏幕
 */
void password_screen_exit(void)
{
    g_password_busy = 1;
    if (g_idle_timer) {
        lv_timer_del(g_idle_timer);
        g_idle_timer = NULL;
    }
    g_value_label = NULL;
    g_error_label = NULL;
    lv_async_call(async_exit_to_main_cb, NULL);
}

/**
 * @brief  密码界面按键处理
 *
 * 运行在button_scan_task中，不直接操作LVGL对象。
 *
 * @param  button_id: BUTTON_ID_OK/UP/DOWN/SHIFT
 * @param  event: BUTTON_EVENT_SHORT/LONG
 */
void password_button_handler(uint8_t button_id, uint8_t event)
{
    g_last_key_tick = lv_tick_get();

    /* SHIFT长按: 取消，返回主屏幕 */
    if (event == BUTTON_EVENT_LONG && button_id == BUTTON_ID_SHIFT) {
        password_screen_exit();
        return;
    }

    /* 非短按事件忽略 */
    if (event != BUTTON_EVENT_SHORT) return;

    /* 过渡保护期间屏蔽按键 */
    if (g_password_busy) return;

    switch (button_id) {
    case BUTTON_ID_UP: {
        /* 当前位数字+1，9循环回0 */
        uint32_t step = g_step_list[g_step_index];
        uint32_t digit = (g_input_value / step) % 10;
        digit = (digit + 1) % 10;
        /* 重建数值: 保留高位和低位，替换当前位 */
        g_input_value = (g_input_value / (step * 10)) * (step * 10)
                      + digit * step
                      + (g_input_value % step);
        /* 异步更新显示 */
        g_password_busy = 1;
        lv_async_call(async_update_value_cb, NULL);
        break;
    }
    case BUTTON_ID_DOWN: {
        /* 当前位数字-1，0循环回9 */
        uint32_t step = g_step_list[g_step_index];
        uint32_t digit = (g_input_value / step) % 10;
        digit = (digit == 0) ? 9 : (digit - 1);
        g_input_value = (g_input_value / (step * 10)) * (step * 10)
                      + digit * step
                      + (g_input_value % step);
        g_password_busy = 1;
        lv_async_call(async_update_value_cb, NULL);
        break;
    }
    case BUTTON_ID_SHIFT: {
        /* 切换高亮位: 千→百→十→个→千 (从左到右) */
        g_step_index = (g_step_index == 0) ? (PASSWORD_DIGITS - 1) : (g_step_index - 1);
        g_password_busy = 1;
        lv_async_call(async_update_value_cb, NULL);
        break;
    }
    case BUTTON_ID_OK: {
        /* 验证密码 */
        if (g_input_value == PASSWORD_VALUE) {
            /* 密码正确 */
            g_password_busy = 1;
            if (g_idle_timer) {
                lv_timer_del(g_idle_timer);
                g_idle_timer = NULL;
            }
            lv_async_call(async_password_correct_cb, NULL);
        } else {
            /* 密码错误 */
            g_password_busy = 1;
            lv_async_call(async_password_wrong_cb, NULL);
        }
        break;
    }
    default:
        break;
    }
}

/*============================================================================*/
/*                          屏幕创建                                            */
/*============================================================================*/

/**
 * @brief  创建密码验证屏幕
 *
 * 屏幕布局 (320x240):
 *   +------------------------------------------+
 *   | < Password                              |  顶栏 36px
 *   +------------------------------------------+
 *   |                                          |
 *   |              0 0 0 0                     | 4位数字 (font_48)
 *   |                                          |
 *   |         [密码错误提示]                    | 错误时显示
 *   |                                          |
 *   +------------------------------------------+
 *   | OK:Confirm  UP/DN  SHIFT:Digit           | 底栏 30px
 *   +------------------------------------------+
 */
static lv_obj_t *create_password_screen(void)
{
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
    lv_label_set_text(title_label, LV_SYMBOL_WARNING "  Password");
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);

    /* --- 居中内容区 --- */
    lv_obj_t *content = lv_obj_create(screen);
    if (content == NULL) { lv_obj_del(screen); return NULL; }
    ui_container_style_init(content);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 36 - 30);
    lv_obj_set_pos(content, 0, 36);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 数字输入标签 */
    g_value_label = lv_label_create(content);
    lv_label_set_recolor(g_value_label, true);
    lv_obj_set_style_text_color(g_value_label, lv_color_hex(COLOR_TEXT_SEL), 0);
    lv_obj_set_style_text_font(g_value_label, &lv_font_montserrat_48, 0);
    update_password_display();

    /* 错误提示标签 (初始隐藏) */
    g_error_label = lv_label_create(content);
    lv_label_set_text(g_error_label, "");
    lv_obj_set_style_text_color(g_error_label, lv_color_hex(COLOR_STEP_HL), 0);
    lv_obj_set_style_text_font(g_error_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_margin_top(g_error_label, 15, 0);

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
    lv_label_set_text(h_ok, "OK:Confirm");
    lv_obj_set_style_text_color(h_ok, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h_ok, &lv_font_montserrat_14, 0);

    lv_obj_t *h_up = lv_label_create(bottom_bar);
    lv_label_set_text(h_up, "UP");
    lv_obj_set_style_text_color(h_up, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h_up, &lv_font_montserrat_14, 0);

    lv_obj_t *h_down = lv_label_create(bottom_bar);
    lv_label_set_text(h_down, "DOWN");
    lv_obj_set_style_text_color(h_down, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h_down, &lv_font_montserrat_14, 0);

    lv_obj_t *h_shift = lv_label_create(bottom_bar);
    lv_label_set_text(h_shift, "SHIFT:Digit");
    lv_obj_set_style_text_color(h_shift, lv_color_hex(COLOR_TEXT_NORMAL), 0);
    lv_obj_set_style_text_font(h_shift, &lv_font_montserrat_14, 0);

    return screen;
}

/*============================================================================*/
/*                          显示更新                                            */
/*============================================================================*/

/**
 * @brief  更新密码数字显示 (带高亮位标记)
 *
 * 在LVGL上下文中调用。将g_input_value格式化为4位数字字符串，
 * 当前编辑位用红色高亮。
 *
 * 高亮位索引映射:
 *   g_step_index = 0 → 个位 (pos_from_left = 3)
 *   g_step_index = 1 → 十位 (pos_from_left = 2)
 *   g_step_index = 2 → 百位 (pos_from_left = 1)
 *   g_step_index = 3 → 千位 (pos_from_left = 0)
 */
static void update_password_display(void)
{
    if (g_value_label == NULL) return;

    char val_str[8];
    snprintf(val_str, sizeof(val_str), "%04lu", (unsigned long)g_input_value);

    /* 计算高亮位: step_index=0是个位(最右), 转换为从左数的位置 */
    uint8_t highlight_pos = PASSWORD_DIGITS - 1 - g_step_index;

    char buf[32];
    int bi = 0;
    for (int i = 0; i < PASSWORD_DIGITS; i++) {
        if (i == highlight_pos) {
            /* 高亮位: 红色 */
            bi += snprintf(buf + bi, sizeof(buf) - bi,
                          "#%06x %c#", (unsigned int)COLOR_STEP_HL, val_str[i]);
        } else {
            /* 普通位 */
            buf[bi++] = val_str[i];
        }
    }
    buf[bi] = '\0';

    lv_label_set_text(g_value_label, buf);
}

/*============================================================================*/
/*                          屏幕切换辅助                                        */
/*============================================================================*/

/**
 * @brief  带智能自动删除的屏幕加载
 *
 * 保护主屏幕不被auto-delete释放。
 */
static void password_screen_load(lv_obj_t *new_screen, lv_screen_load_anim_t anim, uint32_t time)
{
    uint8_t auto_del = (ui_manager->active_screen != ui_manager->main_screen);
    lv_screen_load_anim(new_screen, anim, time, 0, auto_del);
    ui_manager->active_screen = new_screen;
}

/*============================================================================*/
/*                          异步回调 (LVGL上下文)                               */
/*============================================================================*/

/**
 * @brief  异步: 创建并显示密码界面
 */
static void async_enter_password_cb(void *context)
{
    (void)context;

    lv_obj_t *screen = create_password_screen();
    if (screen == NULL) {
        g_password_busy = 0;
        return;
    }

    ui_manager->password_screen = screen;
    password_screen_load(screen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, ANIM_TIME);
    g_password_busy = 0;
}

/**
 * @brief  异步: 更新密码数字显示
 */
static void async_update_value_cb(void *context)
{
    (void)context;
    update_password_display();
    g_password_busy = 0;
}

/**
 * @brief  异步: 密码正确，进入设置菜单
 */
static void async_password_correct_cb(void *context)
{
    (void)context;

    g_value_label = NULL;
    g_error_label = NULL;
    ui_manager->password_screen = NULL;

    /* 切换回主屏幕让密码屏幕被释放，然后进入设置 */
    password_screen_load(ui_manager->main_screen, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, ANIM_TIME);
    g_password_busy = 0;

    /* 进入设置菜单 */
    set_page_enter();
}

/**
 * @brief  异步: 密码错误，显示提示并重置输入
 */
static void async_password_wrong_cb(void *context)
{
    (void)context;

    /* 显示错误提示 */
    if (g_error_label) {
        lv_label_set_text(g_error_label, "Wrong Password!");
        lv_obj_set_style_text_color(g_error_label, lv_color_hex(COLOR_STEP_HL), 0);
    }

    /* 重置输入值和步进 */
    g_input_value = 0;
    g_step_index = PASSWORD_DIGITS - 1;
    update_password_display();

    /* 设置定时器延迟清除错误提示 */
    g_password_busy = 0;
}

/**
 * @brief  异步: 返回主屏幕
 */
static void async_exit_to_main_cb(void *context)
{
    (void)context;

    g_value_label = NULL;
    g_error_label = NULL;
    ui_manager->password_screen = NULL;
    password_screen_load(ui_manager->main_screen, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, ANIM_TIME);
    g_password_busy = 0;
}

/*============================================================================*/
/*                          空闲超时                                            */
/*============================================================================*/

/**
 * @brief  空闲超时回调 (LVGL定时器)
 *
 * 15秒无操作自动返回主屏幕。
 */
static void idle_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    if (lv_tick_get() - g_last_key_tick >= IDLE_TIMEOUT_MS) {
        if (g_idle_timer) {
            lv_timer_del(g_idle_timer);
            g_idle_timer = NULL;
        }
        g_value_label = NULL;
        g_error_label = NULL;
        ui_manager->password_screen = NULL;
        password_screen_load(ui_manager->main_screen, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, ANIM_TIME);
        g_password_busy = 0;
    }
}
