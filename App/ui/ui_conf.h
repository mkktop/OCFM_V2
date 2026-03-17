#ifndef __UI_CONF_H__
#define __UI_CONF_H__

#include "lvgl.h"

//瓦片视图管理器
typedef struct{
    // 主屏幕相关
    lv_obj_t *main_screen;//首页屏幕指针
    lv_obj_t *tileview; //瓦片视图
    lv_obj_t *tile1; //第一页瓦片
    lv_obj_t *tile2; //第二页瓦片
    lv_obj_t *tile3; //第三页瓦片
    int current_page; //当前瓦片页
    // 设置屏幕相关
    lv_obj_t *settings_screen;//设置屏幕指针
    //历史记录屏幕指针
    lv_obj_t *history_screen;//历史记录屏幕指针
    // 当前激活的屏幕
    lv_obj_t *active_screen;//当前激活的屏幕指针
}ui_manager_t;

extern ui_manager_t *ui_manager;//ui管理器指针,所有页面均可调用

#endif /* __UI_CONF_H__ */
