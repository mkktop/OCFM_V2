/**
 * @file ui_password.h
 * @brief 密码验证界面头文件
 * @note 进入设置菜单前的密码锁屏界面
 *        输入方式与参数编辑页类似：SHIFT切换位数，UP/DOWN调整数字，OK确认
 */

#ifndef __UI_PASSWORD_H__
#define __UI_PASSWORD_H__

#include "lvgl.h"

/**
 * @brief  进入密码验证界面
 * @note   从主屏幕长按OK触发，替代直接进入设置菜单
 */
void password_screen_enter(void);

/**
 * @brief  退出密码界面，返回主屏幕
 */
void password_screen_exit(void);

/**
 * @brief  密码界面按键处理
 * @param  button_id: 按键ID (BUTTON_ID_OK/UP/DOWN/SHIFT)
 * @param  event: 按键事件类型 (BUTTON_EVENT_SHORT/LONG)
 */
void password_button_handler(uint8_t button_id, uint8_t event);

#endif /* __UI_PASSWORD_H__ */
