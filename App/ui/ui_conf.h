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
    
    // 主屏幕控件
    lv_obj_t *inst_unit_label;            ///< 瞬时流量单位标签

    // 趋势图相关
    lv_obj_t *trend_chart;               ///< 趋势图控件
    lv_chart_series_t *trend_series_10s;  ///< 10秒采样序列 (30点=5分钟历史)
    lv_chart_series_t *trend_series_5min; ///< 5分钟采样序列 (30点=150分钟历史)
    lv_obj_t *trend_max_label;           ///< 底部最大值标签
    uint32_t trend_tick_counter;         ///< 秒计数器
    int32_t trend_y_max;                 ///< 当前Y轴最大值
    float trend_max_flow;                ///< 历史最大瞬时流量 (m³/h)

    // Subjects 层 - LVGL Observer 主题
    struct {
        // 时间相关
        lv_subject_t time_str;
        char time_buf[32], time_prev_buf[32];
        lv_subject_t time_short_str;
        char time_short_buf[16], time_short_prev_buf[16];
        lv_subject_t total_time_str;
        char total_time_buf[32], total_time_prev_buf[32];

        // 流量相关
        lv_subject_t instant_flow_str;
        char instant_flow_buf[16], instant_flow_prev_buf[16];
        lv_subject_t current_ma_str;
        char current_ma_buf[16], current_ma_prev_buf[16];
        lv_subject_t total_flow_str;
        char total_flow_buf[24], total_flow_prev_buf[24];

        // 水位相关
        lv_subject_t water_level_str;
        char water_level_buf[16], water_level_prev_buf[16];

        // 温度相关
        lv_subject_t temperature_str;
        char temperature_buf[16], temperature_prev_buf[16];
    } subjects;
}ui_manager_t;

extern ui_manager_t *ui_manager;//ui管理器指针,所有页面均可调用

/*============================================================================*/
/*                               UI 颜色常量                                   */
/*============================================================================*/

#define COLOR_BG            0x1E272E    /* 屏幕背景 (深色)              */
#define COLOR_ROW_SEL       0x363636    /* 选中行高亮背景              */
#define COLOR_TEXT_SEL      0xFFFFFF    /* 选中项文字 (白色)            */
#define COLOR_TEXT_NORMAL   0xBDC3C7    /* 普通项文字 (浅灰)            */
#define COLOR_ACCENT        0x2effde    /* 强调色 (青绿色)              */
#define COLOR_STEP_HL       0xff3333    /* 步进位高亮 (红色)            */
#define COLOR_BOTTOM_BG     0x253035    /* 底栏背景                    */

#endif /* __UI_CONF_H__ */
