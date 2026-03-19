#include "ui.h"
#include "global.h"
#include "app_model.h"
ui_manager_t *ui_manager;//ui管理器指针,所有页面均可调用

/**
 * @brief 字符串标签 Observer 回调函数
 * @details 当 Subject 的值发生变化时，LVGL 会自动调用此回调函数
 *          该函数会将 Subject 中的字符串内容更新到 Label 控件上
 * 
 * @param observer Observer 对象指针，包含目标控件信息
 * @param subject 发生变化的 Subject 对象指针
 * 
 * @note 这是一个静态（内部）函数，通过 lv_subject_add_observer_obj() 注册
 * 
 * @par 工作原理
 * 1. 从 observer 中获取绑定的 Label 控件
 * 2. 从 subject 中获取当前的字符串内容
 * 3. 将字符串内容设置到 Label 控件上
 * 
 * @see lv_subject_add_observer_obj()
 * @see lv_observer_get_target()
 * @see lv_subject_get_string()
 */
static void string_label_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    // 从 Observer 中获取绑定的 Label 控件对象
    lv_obj_t *label = lv_observer_get_target(observer);
    // 从 Subject 中获取当前的字符串内容
    const char *text = lv_subject_get_string(subject);
    // 将字符串内容设置到 Label 控件上
    lv_label_set_text(label, text);
}

/**
 * @brief UI 更新定时器回调函数
 * @details 每隔一定时间（当前为 1 秒）被 LVGL 定时器调用
 *          该函数负责更新数据模型中的数据，并同步到 Subject
 * 
 * @param timer 定时器对象指针
 * 
 * @note 这是一个静态（内部）函数，通过 lv_timer_create() 注册
 * 
 * @par 工作原理
 * 1. 调用 app_model_update() 从 RTC 获取最新时间
 * 2. 将更新后的时间字符串复制到 time_str Subject
 * 3. 将更新后的累计时间字符串复制到 record_time_str Subject
 * 4. Subject 会自动触发绑定的 Observer 回调，更新 UI 控件
 * 
 * @warning 该函数中不能执行耗时操作，否则会影响 UI 响应
 * 
 * @see lv_timer_create()
 * @see app_model_update()
 * @see lv_subject_copy_string()
 */
static void ui_update_timer_cb(lv_timer_t *timer)
{
    // 从 RTC 获取最新数据并更新到数据模型中
    app_model_update();

    // 将当前时间字符串复制到 Subject（触发 Observer 回调更新 UI）
    lv_subject_copy_string(&ui_manager->subjects.time_str, g_app_model.time_str);
    // 将简短时间字符串复制到 Subject（触发 Observer 回调更新 UI）
    lv_subject_copy_string(&ui_manager->subjects.time_short_str, g_app_model.time_short_str);
    // 将累计记录时间字符串复制到 Subject（触发 Observer 回调更新 UI）
    lv_subject_copy_string(&ui_manager->subjects.record_time_str, g_app_model.record_time_str);
}

/// @brief 初始化容器样式，创建一个没有内外边距圆角的对象
/// @param obj 要初始化的容器对象指针
void ui_container_style_init(lv_obj_t *obj)
{
    //删除内边距
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_pad_row(obj, 0, 0);
    lv_obj_set_style_pad_column(obj, 0, 0);
    //删除边框和圆角
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
}

/// @brief 创建一个屏幕
/// @return 屏幕对象指针
static lv_obj_t *init_screen(void)
{
    lv_obj_t *obj = lv_obj_create(NULL);
    ui_container_style_init(obj);
    return obj;
}

/// @brief 创建一个瓦片视图
/// @param tileview
/// @return
lv_obj_t *ui_create_tileview(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_tileview_create(parent);
    lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);
    //- LV_HOR_RES ：水平分辨率（屏幕宽度）
    //- LV_VER_RES ：垂直分辨率（屏幕高度）
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);//关闭滚动条
    return obj;
}

/// @brief 创建一个瓦片
/// @param parent 父对象指针
/// @param x 瓦片在网格中的x坐标
/// @param y 瓦片在网格中的y坐标
/// @param dir 瓦片的方向
/// @return 瓦片对象指针
static lv_obj_t *ui_create_tile(lv_obj_t *parent, int x, int y, lv_dir_t dir)
{
    lv_obj_t *obj = lv_tileview_add_tile(parent, x, y, dir);
    ui_container_style_init(obj);
    return obj;
}

/// @brief 创建一个详情页瓦片
/// @param tile 详情页瓦片对象指针
static void create_details_tile(lv_obj_t *tile)
{
    //设置背景颜色和透明度
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E272E), 0);//设置背景颜色为0x1E272E
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);//设置背景透明度为覆盖，即完全不透明
    //设置布局为flex
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);//设置布局为弹性布局
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);//设置布局为flex列方向
    lv_obj_set_style_pad_all(tile, 0, 0);//删除所有内边距
    lv_obj_set_style_pad_row(tile, 0, 0);//删除行内边距

    //创建顶部栏容器
    lv_obj_t *top_bar = lv_obj_create(tile);
    lv_obj_set_size(top_bar, LV_PCT(100), 30); //设置时间容器宽度为100%，高度为30px
    ui_container_style_init(top_bar);//初始化时间容器样式
    lv_obj_set_layout(top_bar, LV_LAYOUT_FLEX); //设置时间容器布局为flex
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START); //设置时间容器子靠左居中对齐
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x1E272E), 0); //设置时间容器背景颜色
    //创建时间标签
    lv_obj_t *time_label = lv_label_create(top_bar);//创建时间显示标签
    lv_label_set_text(time_label, " 14:30");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x2effde), 0);//设置时间标签文本颜色
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);//设置时间标签字体
    lv_subject_add_observer_obj(&ui_manager->subjects.time_short_str, string_label_observer_cb, time_label, NULL);//绑定时间标签到简短时间 Subject
    //创建一个弹性空间，用于分隔时间标签和闹钟标签
    lv_obj_t *spacer = lv_obj_create(top_bar);//创建弹性空间
    lv_obj_set_flex_grow(spacer, 1);//设置弹性空间flex增长系数为1，用于分隔时间标签和闹钟标签
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);//设置弹性空间背景透明度为透明
    lv_obj_set_style_border_width(spacer, 0, 0);//设置弹性空间边框宽度为0
    lv_obj_set_height(spacer, 30);//设置弹性空间高度为30px
    //创建报警标签
    lv_obj_t *alarm_label = lv_label_create(top_bar);//创建报警标签
    lv_label_set_text(alarm_label, "UP alarm");//设置报警标签文本
    lv_obj_set_style_text_font(alarm_label, &lv_font_montserrat_24, 0);//设置报警标签字体
    lv_obj_set_style_text_color(alarm_label, lv_color_hex(0xFF0000), 0);//设置报警标签文本颜色


    //创建瞬时流量容器
    lv_obj_t *inst_flaw_obj = lv_obj_create(tile);//创建瞬时流量容器
    lv_obj_set_size(inst_flaw_obj, LV_PCT(100), 80);//设置瞬时流量容器宽度为100%，高度为50px
    ui_container_style_init(inst_flaw_obj);//初始化瞬时流量容器样式
    lv_obj_set_style_margin_left(inst_flaw_obj, 10, 0);//设置左边距为5像素
    lv_obj_set_style_margin_right(inst_flaw_obj, 10, 0);//设置右边距为5像素
    lv_obj_set_layout(inst_flaw_obj, LV_LAYOUT_FLEX);//设置瞬时流量容器布局为flex
    lv_obj_set_flex_align(inst_flaw_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);//设置瞬时流量容器子容器居中对齐
    lv_obj_set_style_bg_color(inst_flaw_obj, lv_color_hex(0x363636), 0);//设置瞬时流量容器背景颜色
    lv_obj_set_flex_flow(inst_flaw_obj, LV_FLEX_FLOW_COLUMN);//设置瞬时流量容器布局为flex列方向
    //创建瞬时流量标签
    lv_obj_t *inst_flaw_label = lv_label_create(inst_flaw_obj);//创建瞬时流量标签
    lv_obj_set_size(inst_flaw_label, LV_PCT(90), 20);//设置瞬时流量标签宽度为50%，高度为80px
    lv_label_set_text(inst_flaw_label, "INST:L/min");//设置瞬时流量标签文本
    lv_obj_set_style_text_font(inst_flaw_label, &lv_font_montserrat_16, 0);//设置瞬时流量标签字体
    lv_obj_set_style_text_color(inst_flaw_label, lv_color_hex(0x2effde), 0);//设置瞬时流量标签文本颜色
    //创建瞬时流量data标签
    lv_obj_t *inst_flaw_data_label = lv_label_create(inst_flaw_obj);//创建瞬时流量data标签
    lv_obj_set_size(inst_flaw_data_label, LV_PCT(90), 50);//设置瞬时流量data标签宽度为50%
    lv_label_set_text(inst_flaw_data_label, "87.287");//设置瞬时流量data标签文本
    lv_obj_set_style_text_font(inst_flaw_data_label, &lv_font_montserrat_48, 0);//设置瞬时流量data标签字体
    lv_obj_set_style_text_color(inst_flaw_data_label, lv_color_hex(0xFFFFFF), 0);//设置瞬时流量data标签文本颜色
    lv_obj_set_style_text_align(inst_flaw_data_label, LV_TEXT_ALIGN_CENTER, 0);//设置瞬时流量datadata标签文本居中对齐
    lv_obj_set_style_bg_color(inst_flaw_data_label, lv_color_hex(0x4F4F4F), 0);//设置瞬时流量data标签背景颜色
    lv_obj_set_style_bg_opa(inst_flaw_data_label, LV_OPA_COVER, 0);//设置瞬时流量data标签背景透明度为覆盖，即完全不透明

    //创建累计流量容器
    lv_obj_t *total_flaw_obj = lv_obj_create(tile);//创建累计流量容器
    lv_obj_set_size(total_flaw_obj, LV_PCT(100), 70);//设置累计流量容器宽度为100%，高度为50px
    ui_container_style_init(total_flaw_obj);//初始化累计流量容器样式
    lv_obj_set_style_margin_left(total_flaw_obj, 10, 0);//设置左边距为5像素
    lv_obj_set_style_margin_right(total_flaw_obj, 10, 0);//设置右边距为5像素
    lv_obj_set_style_margin_bottom(total_flaw_obj, 10, 0);//设置下边距为10像素
    lv_obj_set_layout(total_flaw_obj, LV_LAYOUT_FLEX);//设置累计流量容器布局为flex
    lv_obj_set_flex_align(total_flaw_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);//设置累计流量容器子容器居中对齐
    lv_obj_set_style_bg_color(total_flaw_obj, lv_color_hex(0x363636), 0);//设置累计流量容器背景颜色
    lv_obj_set_flex_flow(total_flaw_obj, LV_FLEX_FLOW_COLUMN);//设置累计流量容器布局为flex列方向
    //创建累计流量标签
    lv_obj_t *total_flaw_label = lv_label_create(total_flaw_obj);//创建累计流量标签
    lv_obj_set_size(total_flaw_label, LV_PCT(90), 20);//设置累计流量标签宽度为50%，高度为80px
    lv_label_set_text(total_flaw_label, "TOTAL:L/min");//设置累计流量标签文本
    lv_obj_set_style_text_font(total_flaw_label, &lv_font_montserrat_16, 0);//设置累计流量标签字体
    lv_obj_set_style_text_color(total_flaw_label, lv_color_hex(0x2effde), 0);//设置累计流量标签文本颜色
    //创建累计流量data标签
    lv_obj_t *total_flaw_data_label = lv_label_create(total_flaw_obj);//创建累计流量data标签
    lv_obj_set_size(total_flaw_data_label, LV_PCT(90), 30);//设置累计流量data标签宽度为50%
    lv_label_set_text(total_flaw_data_label, "565426374.223");//设置累计流量data标签文本
    lv_obj_set_style_text_font(total_flaw_data_label, &lv_font_montserrat_26, 0);//设置累计流量data标签字体
    lv_obj_set_style_text_color(total_flaw_data_label, lv_color_hex(0xFFFFFF), 0);//设置累计流量data标签文本颜色
    lv_obj_set_style_text_align(total_flaw_data_label, LV_TEXT_ALIGN_CENTER, 0);//设置累计流量data标签文本居中对齐
    lv_obj_set_style_bg_color(total_flaw_data_label, lv_color_hex(0x4F4F4F), 0);//设置累计流量data标签背景颜色
    lv_obj_set_style_bg_opa(total_flaw_data_label, LV_OPA_COVER, 0);//设置累计流量data标签背景透明度为覆盖，即完全不透明


    //创建底部容器
    lv_obj_t *bottom_obj = lv_obj_create(tile);//创建底部容器
    ui_container_style_init(bottom_obj);//初始化底部容器样式
    lv_obj_set_size(bottom_obj, LV_PCT(100), 50);//设置底部容器宽度为100%，高度为70px
    ui_container_style_init(bottom_obj);//初始化底部容器样式
    lv_obj_set_layout(bottom_obj, LV_LAYOUT_FLEX);//设置底部容器布局为flex
    lv_obj_set_flex_align(bottom_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);//设置底部容器子容器居中对齐
    lv_obj_set_style_bg_color(bottom_obj, lv_color_hex(0x1E272E), 0);//设置底部容器背景颜色
    lv_obj_set_flex_flow(bottom_obj, LV_FLEX_FLOW_ROW);//设置底部容器布局为flex行方向
    //创建第一个子容器
    lv_obj_t *bottom_child1 = lv_obj_create(bottom_obj);//创建第一个子容器
    ui_container_style_init(bottom_child1);//初始化第一个子容器样式
    lv_obj_set_size(bottom_child1, LV_PCT(50), LV_PCT(100));//设置子容器宽度为50%，高度为父容器100%
    lv_obj_set_style_bg_color(bottom_child1, lv_color_hex(0x1E272E), 0);//设置子容器背景颜色
    lv_obj_set_style_border_width(bottom_child1, 0, 0);//移除边框
    lv_obj_set_layout(bottom_child1, LV_LAYOUT_FLEX);//设置子容器布局为flex
    lv_obj_set_flex_align(bottom_child1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);//设置子容器子元素居中对齐
    //lv_obj_set_scrollbar_mode(bottom_child1, LV_SCROLLBAR_MODE_OFF);//关闭滚动条
    //创建4-20ma标签
    lv_obj_t *bottom_child1_label = lv_label_create(bottom_child1);//创建4-20ma标签
    //lv_obj_set_size(bottom_child1_label, LV_PCT(90), 50);//设置4-20ma标签宽度为50%
    lv_label_set_text(bottom_child1_label, "16.2ma");//设置4-20ma标签文本
    lv_obj_set_style_text_font(bottom_child1_label, &lv_font_montserrat_26, 0);//设置4-20ma标签字体
    lv_obj_set_style_text_color(bottom_child1_label, lv_color_hex(0x2effde), 0);//设置4-20ma标签文本颜色
    lv_obj_set_style_text_align(bottom_child1_label, LV_TEXT_ALIGN_CENTER, 0);//设置4-20ma标签文本居中对齐
    lv_obj_set_style_bg_color(bottom_child1_label, lv_color_hex(0x8B8B7A), 0);//设置4-20ma标签背景颜色



    //创建第二个子容器
    lv_obj_t *bottom_child2 = lv_obj_create(bottom_obj);//创建第二个子容器
    ui_container_style_init(bottom_child2);//初始化第二个子容器样式
    lv_obj_set_size(bottom_child2, LV_PCT(50), LV_PCT(100));//设置子容器宽度为50%，高度为父容器100%
    lv_obj_set_style_bg_color(bottom_child2, lv_color_hex(0x1E272E), 0);//设置子容器背景颜色
    lv_obj_set_style_border_width(bottom_child2, 0, 0);//移除边框
    lv_obj_set_layout(bottom_child2, LV_LAYOUT_FLEX);//设置子容器布局为flex
    lv_obj_set_flex_align(bottom_child2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);//设置子容器子元素居中对齐
    //lv_obj_set_scrollbar_mode(bottom_child2, LV_SCROLLBAR_MODE_OFF);//关闭滚动条
    //创建温度标签
    lv_obj_t *bottom_child2_label = lv_label_create(bottom_child2);//创建温度标签
    //lv_obj_set_size(bottom_child2_label, LV_PCT(90), 50);//设置温度标签宽度为50%
    lv_label_set_text(bottom_child2_label, "25.2°C");//设置温度标签文本
    lv_obj_set_style_text_font(bottom_child2_label, &lv_font_montserrat_26, 0);//设置温度标签字体
    lv_obj_set_style_text_color(bottom_child2_label, lv_color_hex(0x2effde), 0);//设置温度标签文本颜色
    lv_obj_set_style_text_align(bottom_child2_label, LV_TEXT_ALIGN_CENTER, 0);//设置温度标签文本居中对齐
    lv_obj_set_style_bg_color(bottom_child2_label, lv_color_hex(0x8B8B7A), 0);//设置温度标签背景颜色
}

/// @brief 创建一个趋势图表详情页瓦片
/// @param tile 趋势图表详情页瓦片对象指针
static void create_trend_chart_tile(lv_obj_t *tile){
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    lv_obj_t *chart = lv_chart_create(tile);
    lv_obj_set_size(chart, LV_PCT(100), LV_PCT(100));
    lv_obj_center(chart);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 10);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, 9);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);

    lv_chart_series_t *ser1 = lv_chart_add_series(chart, lv_color_hex(0x3498DB), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *ser2 = lv_chart_add_series(chart, lv_color_hex(0x2ECC71), LV_CHART_AXIS_PRIMARY_Y);

    lv_chart_set_next_value(chart, ser1, 30);
    lv_chart_set_next_value(chart, ser1, 45);
    lv_chart_set_next_value(chart, ser1, 35);
    lv_chart_set_next_value(chart, ser1, 50);
    lv_chart_set_next_value(chart, ser1, 40);
    lv_chart_set_next_value(chart, ser1, 60);
    lv_chart_set_next_value(chart, ser1, 55);
    lv_chart_set_next_value(chart, ser1, 55);
    lv_chart_set_next_value(chart, ser1, 50);
    lv_chart_set_next_value(chart, ser1, 40);

    lv_chart_set_next_value(chart, ser2, 20);
    lv_chart_set_next_value(chart, ser2, 35);
    lv_chart_set_next_value(chart, ser2, 25);
    lv_chart_set_next_value(chart, ser2, 40);
    lv_chart_set_next_value(chart, ser2, 30);
    lv_chart_set_next_value(chart, ser2, 50);
    lv_chart_set_next_value(chart, ser2, 45);
    lv_chart_set_next_value(chart, ser2, 60);
    lv_chart_set_next_value(chart, ser2, 55);
    lv_chart_set_next_value(chart, ser2, 70);

    lv_obj_set_style_bg_color(chart, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 5, 0);
}

/// @brief 创建一个累计流量记录页瓦片
/// @param tile 累计流量记录页瓦片对象指针
static void create_flow_record_tile(lv_obj_t *tile){
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    // 顶部：当前时间
    lv_obj_t *time_label = lv_label_create(tile);
    lv_label_set_text(time_label, "2026/03/02 12:30:45");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x2effde), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_subject_add_observer_obj(&ui_manager->subjects.time_str, string_label_observer_cb, time_label, NULL);

    // 中间：累计流量记录总时间
    lv_obj_t *record_time_label = lv_label_create(tile);
    lv_label_set_text(record_time_label, "total time");
    lv_obj_set_style_text_color(record_time_label, lv_color_hex(0xBDC3C7), 0);
    lv_obj_set_style_text_font(record_time_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(record_time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(record_time_label, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *record_time_value = lv_label_create(tile);
    lv_label_set_text(record_time_value, "125 day 08:30:15");
    lv_obj_set_style_text_color(record_time_value, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_text_font(record_time_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(record_time_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(record_time_value, LV_ALIGN_CENTER, 0, 0);
    lv_subject_add_observer_obj(&ui_manager->subjects.record_time_str, string_label_observer_cb, record_time_value, NULL);

    // 底部：累计流量
    lv_obj_t *total_flow_label = lv_label_create(tile);
    lv_label_set_text(total_flow_label, "sum flow");
    lv_obj_set_style_text_color(total_flow_label, lv_color_hex(0xBDC3C7), 0);
    lv_obj_set_style_text_font(total_flow_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(total_flow_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(total_flow_label, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *total_flow_value = lv_label_create(tile);
    lv_label_set_text(total_flow_value, "1250.8 m3/h");
    lv_obj_set_style_text_color(total_flow_value, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_style_text_font(total_flow_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(total_flow_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(total_flow_value, LV_ALIGN_CENTER, 0, 70);
}

/// @brief 切换屏幕
/// @param new_screen 新屏幕对象指针
/// @param anim_type 屏幕切换动画类型
/// @param time 屏幕切换动画时间
void ui_switch_screen(lv_obj_t *new_screen, lv_screen_load_anim_t anim_type, uint32_t time) {
    //TODO:未来加入密码验证界面,验证通过后再切换屏幕(从主页切换到其他页面需要验证)
    if(ui_manager == NULL || new_screen == NULL) return;
    // 执行屏幕切换
    lv_screen_load_anim(new_screen, anim_type, time, 0, true);
    // 更新激活屏幕指针
    ui_manager->active_screen = new_screen;
}

/// @brief 切换到指定瓦片页
/// @param page_index 瓦片页索引 (0, 1, 2)
void ui_switch_tile(uint8_t page_index) {
    if(ui_manager == NULL || ui_manager->tileview == NULL) return;
    
    lv_obj_t *target_tile = NULL;
    switch(page_index) {
        case 0:
            target_tile = ui_manager->tile1;
            break;
        case 1:
            target_tile = ui_manager->tile2;
            break;
        case 2:
            target_tile = ui_manager->tile3;
            break;
        default:
            return;
    }
    
    lv_tileview_set_tile(ui_manager->tileview, target_tile, LV_ANIM_ON);
    ui_manager->current_page = page_index;
}


/// @brief 创建UI
/// @details 初始化UI管理器,创建首页屏幕,将活动屏幕切换到首页屏幕,创建首页瓦片视图,初始化首页瓦片页面,初始化设置屏幕,初始化历史记录屏幕
void ui_create(void)
{
    app_model_init();

    //初始化UI管理器
    ui_manager = lv_malloc_zeroed(sizeof(ui_manager_t));
    
    lv_subject_init_string(&ui_manager->subjects.time_str, 
                           ui_manager->subjects.time_buf, 
                           ui_manager->subjects.time_prev_buf, 
                           sizeof(ui_manager->subjects.time_buf), 
                           "");
    lv_subject_init_string(&ui_manager->subjects.time_short_str, 
                           ui_manager->subjects.time_short_buf, 
                           ui_manager->subjects.time_short_prev_buf, 
                           sizeof(ui_manager->subjects.time_short_buf), 
                           "");
    lv_subject_init_string(&ui_manager->subjects.record_time_str, 
                           ui_manager->subjects.record_time_buf, 
                           ui_manager->subjects.record_time_prev_buf, 
                           sizeof(ui_manager->subjects.record_time_buf), 
                           "");
    lv_subject_init_float(&ui_manager->subjects.total_flow, 0.0f);

    //初始化首页屏幕
    ui_manager->main_screen = init_screen();
    //将活动屏幕切换到首页屏幕
    ui_switch_screen(ui_manager->main_screen, LV_SCREEN_LOAD_ANIM_NONE, 0);//无动画加载首页屏幕
    //创建首页瓦片视图
    ui_manager->tileview = ui_create_tileview(ui_manager->main_screen);//创建首页瓦片视图
    ui_manager->tile1 = ui_create_tile(ui_manager->tileview, 0, 0, LV_DIR_ALL);//创建第一页瓦片
    ui_manager->tile2 = ui_create_tile(ui_manager->tileview, 0, 1, LV_DIR_ALL);//创建第二页瓦片
    ui_manager->tile3 = ui_create_tile(ui_manager->tileview, 0, 2, LV_DIR_ALL);//创建第三页瓦片
    ui_manager->current_page = 0;//初始化当前瓦片页为0
    //初始化首页瓦片页面
    create_details_tile(ui_manager->tile1);//瓦片一为详情页
    //创建趋势图表详情页瓦片样式
    create_trend_chart_tile(ui_manager->tile2);//瓦片二为趋势图表详情页
    //创建累计流量记录页瓦片样式
    create_flow_record_tile(ui_manager->tile3);//瓦片三为累计流量记录页
    //初始化设置屏幕
    ui_manager->settings_screen = init_screen();
    //初始化历史记录屏幕
    ui_manager->history_screen = init_screen();
    //创建了一个 LVGL 定时器，用于定期更新 UI 数据
    lv_timer_create(ui_update_timer_cb, 1000, NULL);
}
