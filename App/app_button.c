/**
 * @file app_button.c
 * @brief 应用层按键处理源文件
 */

#include "app_button.h"
#include "button_driver.h"
#include <stdio.h>
#include "global.h"
#include "ui/ui_set_page.h"
#include "ui/ui_password.h"

/**
 * @brief 按键事件处理入口
 * @param button_id: 按键ID
 * @param event: 按键事件类型
 * @retval None
 */
void app_button_event_handler(ButtonId_e button_id, ButtonEvent_e event)
{
    if (ui_manager->active_screen == ui_manager->main_screen)
    {
        app_main_screen_button_handler(button_id, event);
    }
    else if (ui_manager->active_screen == ui_manager->password_screen)
    {
        app_password_screen_button_handler(button_id, event);
    }
    else if (ui_manager->active_screen == ui_manager->settings_screen)
    {
        app_set_screen_button_handler(button_id, event);
    }
    else if (ui_manager->active_screen == ui_manager->history_screen)
    {
        app_history_screen_button_handler(button_id, event);
    }
    /* settings_screen在动态创建模式下由set_page模块更新指针 */
}

/**
 * @brief 初始化应用层按键
 * @retval None
 */
void app_button_init(void)
{
    button_driver_init(app_button_event_handler);
}


void app_main_screen_button_handler(ButtonId_e button_id, ButtonEvent_e event)
{
    if (event == BUTTON_EVENT_SHORT) {
        /* 短按事件 */
        switch (button_id) {
            case BUTTON_ID_OK:    printf("[Button] OK Short\r\n"); break;
            case BUTTON_ID_UP:    app_main_screen_up_button_handler(); break;
            case BUTTON_ID_DOWN:  app_main_screen_down_button_handler(); break;
            case BUTTON_ID_SHIFT: printf("[Button] SHIFT Short\r\n"); break;
            default: break;
        }
    } else if (event == BUTTON_EVENT_LONG) {
        /* 长按事件 */
        switch (button_id) {
            case BUTTON_ID_OK:    password_screen_enter(); break;
            case BUTTON_ID_UP:    printf("[Button] UP Long\r\n"); break;
            case BUTTON_ID_DOWN:  printf("[Button] DOWN Long\r\n"); break;
            case BUTTON_ID_SHIFT: printf("[Button] SHIFT Long\r\n"); break;
            default: break;
        }
    }
}

/**
 * @brief 设置页面按键处理
 * @param button_id: 按键ID
 * @param event: 按键事件类型
 * @retval None
 * @note  委托给set_page_button_handler()处理导航逻辑
 */
void app_set_screen_button_handler(ButtonId_e button_id, ButtonEvent_e event)
{
    set_page_button_handler((uint8_t)button_id, (uint8_t)event);
}

/**
 * @brief 密码界面按键处理
 * @param button_id: 按键ID
 * @param event: 按键事件类型
 * @retval None
 * @note  委托给password_button_handler()处理
 */
void app_password_screen_button_handler(ButtonId_e button_id, ButtonEvent_e event)
{
    password_button_handler((uint8_t)button_id, (uint8_t)event);
}

/**
 * @brief 历史页面按键处理
 * @param button_id: 按键ID
 * @param event: 按键事件类型
 * @retval None
 */
void app_history_screen_button_handler(ButtonId_e button_id, ButtonEvent_e event)
{
    if (event == BUTTON_EVENT_SHORT) {
        switch (button_id) {
            case BUTTON_ID_OK:    printf("[HistoryPage] OK Short\r\n"); break;
            case BUTTON_ID_UP:    printf("[HistoryPage] UP Short\r\n"); break;
            case BUTTON_ID_DOWN:  printf("[HistoryPage] DOWN Short\r\n"); break;
            case BUTTON_ID_SHIFT: printf("[HistoryPage] SHIFT Short\r\n"); break;
            default: break;
        }
    } else if (event == BUTTON_EVENT_LONG) {
        switch (button_id) {
            case BUTTON_ID_OK:    printf("[HistoryPage] OK Long\r\n"); break;
            case BUTTON_ID_UP:    printf("[HistoryPage] UP Long\r\n"); break;
            case BUTTON_ID_DOWN:  printf("[HistoryPage] DOWN Long\r\n"); break;
            case BUTTON_ID_SHIFT: printf("[HistoryPage] SHIFT Long\r\n"); break;
            default: break;
        }
    }
}

/**
 * @brief 异步切换瓦片页的上下文数据
 */
typedef struct {
    uint8_t page_index;
} tile_switch_context_t;

/**
 * @brief 异步切换瓦片页回调
 * @param context: 上下文数据
 * @retval None
 */
static void async_switch_tile_cb(void *context)
{
    tile_switch_context_t *ctx = (tile_switch_context_t *)context;
    ui_switch_tile(ctx->page_index);
    lv_free(ctx);
}

/**
 * @brief 主页面上键处理 - 瓦片页码减小
 * @retval None
 */
void app_main_screen_up_button_handler(void)
{
    if (ui_manager == NULL) return;

    tile_switch_context_t *ctx = lv_malloc(sizeof(tile_switch_context_t));
    if (ctx == NULL) return;

    /* 当前页码-1，小于0则循环回到2 */
    if (ui_manager->current_page == 0) {
        ctx->page_index = 2;
    } else {
        ctx->page_index = ui_manager->current_page - 1;
    }

    lv_async_call(async_switch_tile_cb, ctx);
}

/**
 * @brief 主页面下键处理 - 瓦片页码增加
 * @retval None
 */
void app_main_screen_down_button_handler(void)
{
    if (ui_manager == NULL) return;

    tile_switch_context_t *ctx = lv_malloc(sizeof(tile_switch_context_t));
    if (ctx == NULL) return;

    /* 当前页码+1，超过2则循环回到0 */
    ctx->page_index = (ui_manager->current_page + 1) % 3;

    lv_async_call(async_switch_tile_cb, ctx);
}
