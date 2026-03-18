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
    
    // Subjects 层 - LVGL Observer 主题
    struct {
        lv_subject_t time_str;          ///< 完整时间字符串，格式：YYYY/MM/DD HH:MM:SS
        lv_subject_t time_short_str;    ///< 简短时间字符串，格式：HH:MM
        lv_subject_t record_time_str;
        lv_subject_t total_flow;
        char time_buf[32];
        char time_prev_buf[32];
        char time_short_buf[16];        ///< 简短时间缓冲区
        char time_short_prev_buf[16];    ///< 简短时间前一个值缓冲区
        char record_time_buf[64];
        char record_time_prev_buf[64];
    } subjects;
}ui_manager_t;

extern ui_manager_t *ui_manager;//ui管理器指针,所有页面均可调用

#endif /* __UI_CONF_H__ */
