#ifndef __UI_H__
#define __UI_H__
#include "ui_conf.h"

void ui_create(void);                                   // 创建UI
lv_obj_t *ui_create_tileview(lv_obj_t *parent);           // 创建瓦片视图
void ui_container_style_init(lv_obj_t *obj);              // 初始化容器样式(无边距圆角)
void ui_switch_screen(lv_obj_t *new_screen, lv_screen_load_anim_t anim_type, uint32_t time);  // 切换屏幕
void ui_switch_tile(uint8_t page_index);                  // 切换到指定瓦片页
void ui_trend_update_range(void);                          // 刷新趋势图Y轴范围

#endif /* __UI_H__ */
