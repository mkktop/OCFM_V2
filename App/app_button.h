/**
 * @file app_button.h
 * @brief 应用层按键处理头文件
 */

#ifndef __APP_BUTTON_H
#define __APP_BUTTON_H

#include "button_driver.h"

/**
 * @brief 按键事件处理入口
 * @param button_id: 按键ID
 * @param event: 按键事件类型
 * @retval None
 */
void app_button_event_handler(ButtonId_e button_id, ButtonEvent_e event);
void app_main_screen_button_handler(ButtonId_e button_id, ButtonEvent_e event);
void app_set_screen_button_handler(ButtonId_e button_id, ButtonEvent_e event);
void app_password_screen_button_handler(ButtonId_e button_id, ButtonEvent_e event);
void app_history_screen_button_handler(ButtonId_e button_id, ButtonEvent_e event);
void app_main_screen_up_button_handler(void);
void app_main_screen_down_button_handler(void);
/**
 * @brief 初始化应用层按键
 * @retval None
 */
void app_button_init(void);

#endif /* __APP_BUTTON_H */
