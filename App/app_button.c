/**
 * @file app_button.c
 * @brief 应用层按键处理源文件
 */

#include "app_button.h"
#include "button_driver.h"
#include <stdio.h>

/**
 * @brief 按键事件处理入口
 * @param button_id: 按键ID
 * @param event: 按键事件类型
 * @retval None
 */
void app_button_event_handler(ButtonId_e button_id, ButtonEvent_e event)
{
    if (event == BUTTON_EVENT_SHORT) {
        /* 短按事件 */
        switch (button_id) {
            case BUTTON_ID_OK:    printf("[Button] OK Short\r\n"); break;
            case BUTTON_ID_UP:    printf("[Button] UP Short\r\n"); break;
            case BUTTON_ID_DOWN:  printf("[Button] DOWN Short\r\n"); break;
            case BUTTON_ID_SHIFT: printf("[Button] SHIFT Short\r\n"); break;
            default: break;
        }
    } else if (event == BUTTON_EVENT_LONG) {
        /* 长按事件 */
        switch (button_id) {
            case BUTTON_ID_OK:    printf("[Button] OK Long\r\n"); break;
            case BUTTON_ID_UP:    printf("[Button] UP Long\r\n"); break;
            case BUTTON_ID_DOWN:  printf("[Button] DOWN Long\r\n"); break;
            case BUTTON_ID_SHIFT: printf("[Button] SHIFT Long\r\n"); break;
            default: break;
        }
    }
}

/**
 * @brief 初始化应用层按键
 * @retval None
 */
void app_button_init(void)
{
    button_driver_init(app_button_event_handler);
}
