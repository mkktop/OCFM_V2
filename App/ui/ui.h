#ifndef __UI_H__
#define __UI_H__
#include "ui_conf.h"


void ui_create(void);

lv_obj_t * ui_create_tileview(lv_obj_t *parent);//创建瓦片视图
void ui_container_style_init(lv_obj_t *obj);//初始化容器样式，创建一个没有内外边距圆角的对象
void ui_switch_tile(uint8_t page_index);//切换到指定瓦片页

#endif /* __UI_H__ */
