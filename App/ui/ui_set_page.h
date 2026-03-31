/**
 * @file ui_set_page.h
 * @brief 设置菜单页面头文件
 * @note  二级菜单架构: 一级菜单(分类) -> 二级菜单(参数列表) -> 编辑页面
 */

#ifndef __UI_SET_PAGE_H__
#define __UI_SET_PAGE_H__

#include "lvgl.h"

/**
 * @brief  进入设置页面 (从主屏幕调用)
 * @retval None
 */
void set_page_enter(void);

/**
 * @brief  退出设置页面，返回主屏幕
 * @retval None
 */
void set_page_exit(void);

/**
 * @brief  设置页面按键处理入口
 * @param  button_id: 按键ID (BUTTON_ID_OK/UP/DOWN/SHIFT)
 * @param  event: 按键事件类型 (BUTTON_EVENT_SHORT/LONG)
 * @retval None
 */
void set_page_button_handler(uint8_t button_id, uint8_t event);

#endif /* __UI_SET_PAGE_H__ */
