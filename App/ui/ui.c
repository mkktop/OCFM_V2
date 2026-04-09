/**
 * @file    ui.c
 * @brief   UI模块实现
 * @details 基于LVGL 9.x实现明渠流量计的用户界面
 *          - 使用Observer模式实现数据绑定
 *          - 瓦片视图布局（详情页、趋势图、记录页）
 */

#include "ui.h"
#include "global.h"
#include "app_model.h"
#include "app_sensor.h"
#include "app_config.h"
#include "app_alarm.h"

ui_manager_t *ui_manager;

/*============================================================================*/
/*                           私有函数                                           */
/*============================================================================*/

/**
 * @brief  获取瞬时流量单位字符串
 * @retval 单位字符串 (如 "L/s", "m³/h")
 */
static const char *get_flow_unit_str(void)
{
    switch (app_config_get_instant_unit()) {
        case FLOW_UNIT_L_S:    return "L/s";
        case FLOW_UNIT_L_MIN:  return "L/min";
        case FLOW_UNIT_L_H:    return "L/h";
        case FLOW_UNIT_M3_H:   return "m\xC2\xB3/h";
        case FLOW_UNIT_M3_S:   return "m\xC2\xB3/s";
        case FLOW_UNIT_M3_MIN: return "m\xC2\xB3/min";
        case FLOW_UNIT_T_H:    return "T/h";
        case FLOW_UNIT_G_H:    return "G/h";
        default:               return "L/s";
    }
}

/**
 * @brief  配置变更回调函数
 * @details 当量程配置变更时，立即刷新趋势图Y轴范围
 * @param id  变更的配置项ID
 */
static void ui_config_change_cb(config_id_t id)
{
    if (id == CONFIG_ID_RANGE_4MA || id == CONFIG_ID_RANGE_20MA)
    {
        ui_trend_update_range();
    }
}

/**
 * @brief  字符串标签Observer回调函数
 * @details 当绑定的Subject值发生变化时，LVGL事件系统会自动调用此函数
 *          该函数是MVVM模式中Observer角色的具体实现
 * 
 * @param observer    Observer对象指针，包含目标Label控件的引用信息
 * @param subject     发生变化的Subject对象指针，包含最新的字符串数据
 * 
 * @note  该函数通过lv_subject_add_observer_obj()注册到Subject上
 *        当任何代码调用lv_subject_copy_string()更新Subject时，此回调会被自动触发
 * 
 * @par 工作流程
 *        1. 从observer中提取目标Label控件对象
 *        2. 从subject中获取当前的字符串内容
 *        3. 调用lv_label_set_text()更新Label显示文本
 * 
 * @see lv_subject_add_observer_obj()
 * @see lv_subject_copy_string()
 * @see lv_label_set_text()
 */
static void string_label_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    /* 从Observer对象中获取绑定的Label控件指针 */
    lv_obj_t *label = lv_observer_get_target(observer);

    /* 从Subject中获取最新的字符串内容 */
    const char *text = lv_subject_get_string(subject);

    /* 更新Label控件的显示文本，实现UI自动刷新 */
    lv_label_set_text(label, text);
}

/**
 * @brief  累计流量Observer回调 (带m³单位后缀，用于记录页)
 */
static void total_flow_with_unit_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *label = lv_observer_get_target(observer);
    const char *text = lv_subject_get_string(subject);
    lv_label_set_text_fmt(label, "%s m\xC2\xB3", text);
}

/**
 * @brief  UI更新定时器回调函数
 * @details 每秒被LVGL定时器调用一次，负责更新界面显示的所有动态数据
 *          该函数是数据流向的关键节点，连接了Model层和View层
 * 
 * @param timer  LVGL定时器对象指针（当前未使用，但回调签名需要）
 * 
 * @note  定时器周期为1000ms，在ui_create()中通过lv_timer_create()创建
 *        该函数执行时间应尽量短，避免阻塞UI线程
 * 
 * @par 数据更新流程
 *        1. 调用app_model_update()从各模块获取最新数据并格式化
 *        2. 将格式化后的字符串复制到对应的Subject中
 *        3. Subject自动触发已注册的Observer回调
 *        4. 各Label控件的文本被自动更新
 * 
 * @par 更新的数据项
 *        - time_str:           当前时间 (YYYY/MM/DD HH:MM:SS)
 *        - time_short_str:     简短时间 (HH:MM)
 *        - total_time_str:    累计运行时长 (N day HH:MM:SS)
 *        - water_level_str:   水位高度 (L:x.xxxm)
 *        - instant_flow_str:  瞬时流量值
 *        - total_flow_str:    累计流量值
 * 
 * @see lv_timer_create()
 * @see app_model_update()
 * @see lv_subject_copy_string()
 */
static void ui_update_timer_cb(lv_timer_t *timer)
{
    /* 第一步：从RTC、传感器、流量计算模块获取最新数据并格式化 */
    app_model_update();

    /* 第二步：将时间数据同步到Subject，触发UI自动更新 */
    lv_subject_copy_string(&ui_manager->subjects.time_str, g_app_model.time_str);
    lv_subject_copy_string(&ui_manager->subjects.time_short_str, g_app_model.time_short_str);
    lv_subject_copy_string(&ui_manager->subjects.total_time_str, g_app_model.total_time_str);

    /* 第三步：将水位数据同步到Subject */
    lv_subject_copy_string(&ui_manager->subjects.water_level_str, g_app_model.water_level_str);

    /* 更新水位标签颜色（离线时变红，在线时恢复青绿色） */
    if (ui_manager->water_level_label != NULL &&
        ui_manager->prev_sensor_online != g_app_model.sensor_online) {
        ui_manager->prev_sensor_online = g_app_model.sensor_online;
        if (g_app_model.sensor_online) {
            lv_obj_set_style_text_color(ui_manager->water_level_label,
                                        lv_color_hex(0x2effde), 0);
        } else {
            lv_obj_set_style_text_color(ui_manager->water_level_label,
                                        lv_color_hex(0xff3333), 0);
        }
    }

    /* 第四步：将流量数据同步到Subject */
    lv_subject_copy_string(&ui_manager->subjects.instant_flow_str, g_app_model.instant_flow_str);
    lv_subject_copy_string(&ui_manager->subjects.current_ma_str, g_app_model.current_ma_str);
    lv_subject_copy_string(&ui_manager->subjects.total_flow_str, g_app_model.total_flow_str);
    lv_subject_copy_string(&ui_manager->subjects.temperature_str, g_app_model.temperature_str);

    /* 更新瞬时流量单位标签 */
    if (ui_manager->inst_unit_label != NULL) {
        lv_label_set_text_fmt(ui_manager->inst_unit_label, "INST:%s", get_flow_unit_str());
    }

    /* 更新底栏报警指示：位图 bit3=HH, bit2=H, bit1=L, bit0=LL */
    if (ui_manager->bottom_alarm_cont != NULL) {
        uint8_t bitmap = 0;
        if (app_alarm_get_state(ALARM_TYPE_AAH) == ALARM_STATE_ACTIVE) bitmap |= 0x08;
        if (app_alarm_get_state(ALARM_TYPE_AH)  == ALARM_STATE_ACTIVE) bitmap |= 0x04;
        if (app_alarm_get_state(ALARM_TYPE_AL)  == ALARM_STATE_ACTIVE) bitmap |= 0x02;
        if (app_alarm_get_state(ALARM_TYPE_AAL) == ALARM_STATE_ACTIVE) bitmap |= 0x01;

        if (bitmap != ui_manager->prev_alarm_bitmap) {
            ui_manager->prev_alarm_bitmap = bitmap;

            /* 各报警标签显隐：HH触发时隐藏H，LL触发时隐藏L */
            if (bitmap & 0x08) lv_obj_clear_flag(ui_manager->alarm_label_hh, LV_OBJ_FLAG_HIDDEN);
            else               lv_obj_add_flag(ui_manager->alarm_label_hh, LV_OBJ_FLAG_HIDDEN);

            if ((bitmap & 0x04) && !(bitmap & 0x08))
                lv_obj_clear_flag(ui_manager->alarm_label_h, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(ui_manager->alarm_label_h, LV_OBJ_FLAG_HIDDEN);

            if ((bitmap & 0x02) && !(bitmap & 0x01))
                lv_obj_clear_flag(ui_manager->alarm_label_l, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(ui_manager->alarm_label_l, LV_OBJ_FLAG_HIDDEN);

            if (bitmap & 0x01) lv_obj_clear_flag(ui_manager->alarm_label_ll, LV_OBJ_FLAG_HIDDEN);
            else               lv_obj_add_flag(ui_manager->alarm_label_ll, LV_OBJ_FLAG_HIDDEN);

            /* 整个报警区：有任一报警则显示，否则隐藏（自动变为两栏） */
            if (bitmap) lv_obj_clear_flag(ui_manager->bottom_alarm_cont, LV_OBJ_FLAG_HIDDEN);
            else        lv_obj_add_flag(ui_manager->bottom_alarm_cont, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 第五步：更新趋势图数据 */
    if (ui_manager->trend_chart != NULL) {
        /* 将瞬时流量从 L/s 转换为 m³/h，精度×100存入图表 */
        float flow_m3h = g_app_model.instant_flow * 3.6f;
        int32_t flow_val = (int32_t)(flow_m3h * 100);
        ui_manager->trend_tick_counter++;
        bool pushed = false;

        /* 更新历史最大值 */
        if (flow_m3h > ui_manager->trend_max_flow) {
            ui_manager->trend_max_flow = flow_m3h;
            lv_label_set_text_fmt(ui_manager->trend_max_label,
                                  "MAX: %.2f m\xC2\xB3/h", ui_manager->trend_max_flow);
        }

        /* 每10秒推入10秒采样序列 */
        if (ui_manager->trend_tick_counter % 10 == 0) {
            lv_chart_set_next_value(ui_manager->trend_chart,
                                    ui_manager->trend_series_10s, flow_val);
            pushed = true;
        }

        /* 每300秒(5分钟)推入5分钟采样序列 */
        if (ui_manager->trend_tick_counter % 300 == 0) {
            lv_chart_set_next_value(ui_manager->trend_chart,
                                    ui_manager->trend_series_5min, flow_val);
            pushed = true;
        }

    }
}

/**
 * @brief  初始化容器对象的基础样式
 * @details 为LVGL容器对象设置统一的默认样式，消除默认的边距、边框和圆角
 *          使容器成为真正的"无边界"容器，便于精确布局
 * 
 * @param obj  目标LVGL对象指针（通常是lv_obj_t类型容器）
 * 
 * @par 设置的样式项
 *        - padding_all:      所有方向内边距设为0
 *        - padding_row:      行间距内边距设为0
 *        - padding_column:   列间距内边距设为0
 *        - border_width:     边框宽度设为0（无边框）
 *        - radius:           圆角半径设为0（直角）
 * 
 * @note  该函数是UI模块的公共样式初始化函数，所有创建的容器都会调用
 *        使用统一的样式初始化可以保证整个界面布局的一致性
 * 
 * @see lv_obj_set_style_pad_all()
 * @see lv_obj_set_style_border_width()
 * @see lv_obj_set_style_radius()
 */
void ui_container_style_init(lv_obj_t *obj)
{
    /* 设置所有方向的内边距为0，消除默认间距 */
    lv_obj_set_style_pad_all(obj, 0, 0);
    
    /* 设置Flex布局的行间距为0 */
    lv_obj_set_style_pad_row(obj, 0, 0);
    
    /* 设置Flex布局的列间距为0 */
    lv_obj_set_style_pad_column(obj, 0, 0);
    
    /* 移除边框，使容器无边界 */
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    
    /* 设置圆角为0，使容器为直角 */
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
}

/**
 * @brief  创建并初始化一个新的屏幕对象
 * @details 屏幕是LVGL中最高层级的容器，所有页面内容都建立在屏幕上
 *          该函数创建一个空白屏幕并应用统一的容器样式
 * 
 * @retval  新创建的屏幕对象指针
 * @retval  NULL（内存分配失败时）
 * 
 * @par 创建流程
 *        1. 调用lv_obj_create(NULL)创建根级屏幕对象
 *        2. 调用ui_container_style_init()应用统一样式
 *        3. 返回屏幕对象供后续页面构建使用
 * 
 * @note  在LVGL中，NULL作为父对象表示创建的是独立屏幕
 *        新创建的屏幕需要通过lv_screen_load_anim()或ui_switch_screen()加载才能显示
 * 
 * @see lv_obj_create()
 * @see ui_container_style_init()
 * @see ui_switch_screen()
 */
static lv_obj_t *init_screen(void)
{
    /* 创建根级屏幕对象，NULL表示它是独立的顶级窗口 */
    lv_obj_t *obj = lv_obj_create(NULL);
    
    /* 应用统一的容器样式，消除默认边距和边框 */
    ui_container_style_init(obj);
    
    /* 返回创建的屏幕对象 */
    return obj;
}

/**
 * @brief  创建瓦片视图容器
 * @details 瓦片视图是一种支持水平或垂直滑动切换页面的容器
 *          类似于移动端的ViewPager或TabView，实现多页面横向滑动切换
 * 
 * @param parent  父对象指针，即瓦片视图所属的屏幕对象
 * 
 * @retval  新创建的瓦片视图对象指针
 * 
 * @par 配置参数
 *        - 尺寸:    设为屏幕分辨率(LV_HOR_RES x LV_VER_RES)，占满整个屏幕
 *        - 滚动条:  关闭滚动条显示，保持界面简洁
 * 
 * @note  瓦片视图本身不包含可见内容，需要通过ui_create_tile()添加具体瓦片页面
 *        每个瓦片可以包含任意LVGL控件，构成完整的页面
 * 
 * @see lv_tileview_create()
 * @see ui_create_tile()
 */
lv_obj_t *ui_create_tileview(lv_obj_t *parent)
{
    /* 创建瓦片视图控件，支持滑动切换多个页面 */
    lv_obj_t *obj = lv_tileview_create(parent);
    
    /* 设置瓦片视图尺寸为全屏大小 */
    lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);
    
    /* 隐藏滚动条，保持界面整洁 */
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    
    return obj;
}

/**
 * @brief  创建单个瓦片（页面）并添加到瓦片视图中
 * @details 瓦片是瓦片视图的子容器，每个瓦片代表一个独立的页面
 *          瓦片按照二维坐标系排列，通过滑动可以切换到相邻的瓦片
 * 
 * @param parent  父对象指针，即瓦片视图对象
 * @param x       瓦片在瓦片视图中的X坐标（列索引）
 * @param y       瓦片在瓦片视图中的Y坐标（行索引）
 * @param dir     允许的滑动方向标志，可组合：
 *                - LV_DIR_NONE:   禁止滑动（静态页面）
 *                - LV_DIR_LEFT:   允许向左滑
 *                - LV_DIR_RIGHT:  允许向右滑
 *                - LV_DIR_UP:     允许向上滑
 *                - LV_DIR_DOWN:   允许向下滑
 *                - LV_DIR_ALL:    允许所有方向滑动
 * 
 * @retval  新创建的瓦片对象指针，可用于添加页面内容
 * 
 * @par 使用示例
 *        ui_create_tile(tileview, 0, 0, LV_DIR_ALL);  // 第一个页面，可任意滑动
 *        ui_create_tile(tileview, 0, 1, LV_DIR_ALL);  // 第二个页面，垂直排列
 *        ui_create_tile(tileview, 0, 2, LV_DIR_ALL);  // 第三个页面
 * 
 * @note  瓦片创建后需要调用ui_switch_tile()或lv_tileview_set_tile()切换显示
 * 
 * @see lv_tileview_add_tile()
 * @see ui_container_style_init()
 */
static lv_obj_t *ui_create_tile(lv_obj_t *parent, int x, int y, lv_dir_t dir)
{
    /* 将新瓦片添加到瓦片视图的指定位置 */
    lv_obj_t *obj = lv_tileview_add_tile(parent, x, y, dir);
    
    /* 应用统一的容器样式 */
    ui_container_style_init(obj);
    
    return obj;
}

/*============================================================================*/
/*                           页面创建函数                                       */
/*============================================================================*/

/**
 * @brief  创建详情页瓦片（流量监测主页）
 * @details 构建流量监测主页面，展示实时水位、瞬时流量、累计流量等核心数据
 *          页面采用垂直Flex布局，分为四个区域：顶部栏、瞬时流量、累计流量、底部栏
 * 
 * @param tile  瓦片对象指针，即详情页的根容器
 * 
 * @par 页面布局结构
 *        ┌────────────────────────────────┐
 *        │  顶部栏(30px)                   │  ← 时间 + 水位
 *        │  HH:MM            L:0.000m      │
 *        ├────────────────────────────────┤
 *        │  瞬时流量区(80px)               │  ← 大字号数据显示
 *        │         0.00                    │
 *        ├────────────────────────────────┤
 *        │  累计流量区(70px)               │  ← 中字号数据显示
 *        │         0.00                   │
 *        ├────────────────────────────────┤
 *        │  底部栏(50px)                   │  ← 4-20mA + 温度
 *        │   16.2ma        25.2°C          │
 *        └────────────────────────────────┘
 * 
 * @par 数据绑定（通过Subject-Observer模式）
 *        - time_short_str   →  顶部时间Label（每小时更新）
 *        - water_level_str  →  顶部水位Label（每秒更新）
 *        - instant_flow_str →  瞬时流量数据Label（每秒更新）
 *        - total_flow_str   →  累计流量数据Label（每秒更新）
 * 
 * @par 样式配置
 *        - 背景色: #1E272E（深灰蓝）
 *        - 主文字: #2effde（青绿色）
 *        - 数据区: #363636（中等灰）
 *        - 数据值: #FFFFFF（白色）
 *        - 字体:   Montserrat 24/48
 * 
 * @note  该函数仅创建UI控件，不执行数据更新
 *        数据更新由ui_update_timer_cb()定时调用完成
 * 
 * @see lv_label_create()
 * @see lv_subject_add_observer_obj()
 * @see ui_update_timer_cb()
 */
static void create_details_tile(lv_obj_t *tile)
{
    /*--------------------------------------------------------------------*/
    /* 第一部分：瓦片基础配置 - 设置背景色和Flex列布局                     */
    /*--------------------------------------------------------------------*/
    
    /* 设置瓦片背景颜色为深灰蓝色 */
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E272E), 0);
    
    /* 设置背景完全不透明 */
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    
    /* 启用Flex布局，按列排列子元素 */
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    
    /* 设置Flex流向为垂直列布局 */
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    
    /* 清除所有内边距 */
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_pad_row(tile, 0, 0);

    /*--------------------------------------------------------------------*/
    /* 第二部分：顶部栏 - 显示当前时间和水位高度                            */
    /*--------------------------------------------------------------------*/
    
    /* 创建顶部栏容器 */
    lv_obj_t *top_bar = lv_obj_create(tile);
    
    /* 设置顶部栏宽度100%，高度30px */
    lv_obj_set_size(top_bar, LV_PCT(100), 30);
    
    /* 应用统一容器样式 */
    ui_container_style_init(top_bar);
    
    /* 启用Flex布局，行内排列 */
    lv_obj_set_layout(top_bar, LV_LAYOUT_FLEX);
    
    /* 设置Flex对齐：左对齐、垂直居中、顶部对齐 */
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    
    /* 设置顶部栏背景色 */
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x1E272E), 0);

    /* 左右留10px边距，与下方流量区对齐 */
    lv_obj_set_style_pad_left(top_bar, 10, 0);
    lv_obj_set_style_pad_right(top_bar, 10, 0);

    /* 创建时间标签 */
    lv_obj_t *time_label = lv_label_create(top_bar);
    lv_label_set_text(time_label, "14:30");
    
    /* 设置时间文字颜色为青绿色 */
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x2effde), 0);
    
    /* 设置时间字体大小为24px */
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
    
    /* 绑定时间Subject，实现每秒自动更新 */
    lv_subject_add_observer_obj(&ui_manager->subjects.time_short_str, 
                                string_label_observer_cb, 
                                time_label, 
                                NULL);

    /* 创建弹性间隔，使时间和水位分布到两端 */
    lv_obj_t *spacer = lv_obj_create(top_bar);
    
    /* 间隔占据所有剩余空间 */
    lv_obj_set_flex_grow(spacer, 1);
    
    /* 设置间隔为透明 */
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_height(spacer, 30);

    /* 创建水位标签 */
    lv_obj_t *water_level_label = lv_label_create(top_bar);
    lv_label_set_text(water_level_label, "L:0.000m");

    /* 设置水位字体大小为24px */
    lv_obj_set_style_text_font(water_level_label, &lv_font_montserrat_24, 0);

    /* 设置水位文字颜色为青绿色 */
    lv_obj_set_style_text_color(water_level_label, lv_color_hex(0x2effde), 0);

    /* 保存水位标签指针，用于离线时更新颜色 */
    ui_manager->water_level_label = water_level_label;

    /* 绑定水位Subject，实现每秒自动更新 */
    lv_subject_add_observer_obj(&ui_manager->subjects.water_level_str, 
                                string_label_observer_cb, 
                                water_level_label, 
                                NULL);

    /*--------------------------------------------------------------------*/
    /* 第三部分：瞬时流量显示区 - 大字号突出显示实时流量                    */
    /*--------------------------------------------------------------------*/
    
    /* 创建瞬时流量容器 */
    lv_obj_t *inst_flaw_obj = lv_obj_create(tile);
    
    /* 设置容器宽度100%，高度80px，左右各留10px边距 */
    lv_obj_set_size(inst_flaw_obj, LV_PCT(100), 80);
    ui_container_style_init(inst_flaw_obj);
    lv_obj_set_style_margin_left(inst_flaw_obj, 10, 0);
    lv_obj_set_style_margin_right(inst_flaw_obj, 10, 0);

    /* 启用Flex垂直列布局，内容居中 */
    lv_obj_set_layout(inst_flaw_obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(inst_flaw_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 设置容器背景色为中等灰色 */
    lv_obj_set_style_bg_color(inst_flaw_obj, lv_color_hex(0x363636), 0);

    /* 左侧青绿色边框 (#2effde) */
    lv_obj_set_style_border_width(inst_flaw_obj, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(inst_flaw_obj, lv_color_hex(0x2effde), LV_PART_MAIN);
    lv_obj_set_style_border_side(inst_flaw_obj, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_flex_flow(inst_flaw_obj, LV_FLEX_FLOW_COLUMN);

    /* 创建瞬时流量单位标签 */
    lv_obj_t *inst_flaw_label = lv_label_create(inst_flaw_obj);
    lv_label_set_text_fmt(inst_flaw_label, "INST:%s", get_flow_unit_str());
    ui_manager->inst_unit_label = inst_flaw_label;

    /* 设置单位标签字体16px，青绿色 */
    lv_obj_set_style_text_font(inst_flaw_label, &my_font_montserrat_16, 0);
    lv_obj_set_style_text_color(inst_flaw_label, lv_color_hex(0x2effde), 0);

    /* 创建瞬时流量数值标签（核心数据显示） */
    lv_obj_t *inst_flaw_data_label = lv_label_create(inst_flaw_obj);
    lv_label_set_text(inst_flaw_data_label, "0.00");

    /* 设置数值字体为48px最大字号，白色，突出显示 */
    lv_obj_set_style_text_font(inst_flaw_data_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(inst_flaw_data_label, lv_color_hex(0xFFFFFF), 0);

    /* 设置文字水平居中对齐 */
    lv_obj_set_style_text_align(inst_flaw_data_label, LV_TEXT_ALIGN_CENTER, 0);
    
    /* 绑定瞬时流量Subject，实现每秒自动更新 */
    lv_subject_add_observer_obj(&ui_manager->subjects.instant_flow_str, 
                                string_label_observer_cb, 
                                inst_flaw_data_label, 
                                NULL);

    /*--------------------------------------------------------------------*/
    /* 第四部分：累计流量显示区 - 中等字号显示累计值                        */
    /*--------------------------------------------------------------------*/
    
    /* 创建累计流量容器 */
    lv_obj_t *total_flaw_obj = lv_obj_create(tile);
    
    /* 设置容器宽度100%，高度70px，四周留边距 */
    lv_obj_set_size(total_flaw_obj, LV_PCT(100), 70);
    ui_container_style_init(total_flaw_obj);
    lv_obj_set_style_margin_left(total_flaw_obj, 10, 0);
    lv_obj_set_style_margin_right(total_flaw_obj, 10, 0);
    lv_obj_set_style_margin_bottom(total_flaw_obj, 10, 0);

    /* 启用Flex垂直列布局，内容居中 */
    lv_obj_set_layout(total_flaw_obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(total_flaw_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 设置容器背景色为中等灰色 */
    lv_obj_set_style_bg_color(total_flaw_obj, lv_color_hex(0x363636), 0);

    /* 左侧蓝色边框 (#3498DB) */
    lv_obj_set_style_border_width(total_flaw_obj, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(total_flaw_obj, lv_color_hex(0x3498DB), LV_PART_MAIN);
    lv_obj_set_style_border_side(total_flaw_obj, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_flex_flow(total_flaw_obj, LV_FLEX_FLOW_COLUMN);

    /* 创建累计流量单位标签 */
    lv_obj_t *total_flaw_label = lv_label_create(total_flaw_obj);
    lv_label_set_text(total_flaw_label, "TOTAL:m\xC2\xB3");

    /* 设置单位标签字体16px，蓝色（匹配左侧边框） */
    lv_obj_set_style_text_font(total_flaw_label, &my_font_montserrat_16, 0);
    lv_obj_set_style_text_color(total_flaw_label, lv_color_hex(0x3498DB), 0);

    /* 创建累计流量数值标签 */
    lv_obj_t *total_flaw_data_label = lv_label_create(total_flaw_obj);
    lv_label_set_text(total_flaw_data_label, "0.00");

    /* 设置数值字体为26px，白色 */
    lv_obj_set_style_text_font(total_flaw_data_label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(total_flaw_data_label, lv_color_hex(0xFFFFFF), 0);

    /* 设置文字水平居中对齐 */
    lv_obj_set_style_text_align(total_flaw_data_label, LV_TEXT_ALIGN_CENTER, 0);
    
    /* 绑定累计流量Subject，实现每秒自动更新 */
    lv_subject_add_observer_obj(&ui_manager->subjects.total_flow_str, 
                                string_label_observer_cb, 
                                total_flaw_data_label, 
                                NULL);

    /*--------------------------------------------------------------------*/
    /* 第五部分：底部栏 - 三栏布局：电流 | 报警 | 温度                      */
    /*        无报警时中间栏隐藏，自动变为两栏                              */
    /*--------------------------------------------------------------------*/

    /* 创建底部栏容器 */
    lv_obj_t *bottom_obj = lv_obj_create(tile);
    lv_obj_set_size(bottom_obj, LV_PCT(100), 50);
    ui_container_style_init(bottom_obj);
    lv_obj_set_layout(bottom_obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(bottom_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_flow(bottom_obj, LV_FLEX_FLOW_ROW);

    /* 底栏背景色 */
    lv_obj_set_style_bg_color(bottom_obj, lv_color_hex(0x363636), 0);
    lv_obj_set_style_bg_opa(bottom_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(bottom_obj, 10, 0);
    lv_obj_set_style_pad_right(bottom_obj, 10, 0);
    lv_obj_clear_flag(bottom_obj, LV_OBJ_FLAG_SCROLLABLE);

    /* 左栏：4-20mA电流（flex_grow=1，与右栏均分剩余空间） */
    lv_obj_t *bottom_left = lv_obj_create(bottom_obj);
    lv_obj_set_size(bottom_left, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(bottom_left, 1);
    ui_container_style_init(bottom_left);
    lv_obj_set_style_bg_opa(bottom_left, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(bottom_left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(bottom_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *bottom_ma_label = lv_label_create(bottom_left);
    lv_label_set_text(bottom_ma_label, "4.00mA");
    lv_obj_set_style_text_font(bottom_ma_label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(bottom_ma_label, lv_color_hex(0x2effde), 0);

    lv_subject_add_observer_obj(&ui_manager->subjects.current_ma_str,
                                string_label_observer_cb,
                                bottom_ma_label,
                                NULL);

    /* 中栏：报警指示（初始隐藏，有报警时由定时器回调显示） */
    lv_obj_t *alarm_cont = lv_obj_create(bottom_obj);
    lv_obj_set_size(alarm_cont, LV_SIZE_CONTENT, LV_PCT(100));
    ui_container_style_init(alarm_cont);
    lv_obj_set_style_bg_opa(alarm_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(alarm_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(alarm_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(alarm_cont, 6, 0);
    lv_obj_set_flex_align(alarm_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(alarm_cont, LV_OBJ_FLAG_HIDDEN);
    ui_manager->bottom_alarm_cont = alarm_cont;

    /* HH 上上限标签 - 红色 */
    lv_obj_t *lbl_hh = lv_label_create(alarm_cont);
    lv_label_set_text(lbl_hh, "HH");
    lv_obj_set_style_text_font(lbl_hh, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_hh, lv_color_hex(0xFF3333), 0);
    lv_obj_add_flag(lbl_hh, LV_OBJ_FLAG_HIDDEN);
    ui_manager->alarm_label_hh = lbl_hh;

    /* H 上限标签 - 橙色 */
    lv_obj_t *lbl_h = lv_label_create(alarm_cont);
    lv_label_set_text(lbl_h, "H");
    lv_obj_set_style_text_font(lbl_h, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_h, lv_color_hex(0xF39C12), 0);
    lv_obj_add_flag(lbl_h, LV_OBJ_FLAG_HIDDEN);
    ui_manager->alarm_label_h = lbl_h;

    /* L 下限标签 - 橙色 */
    lv_obj_t *lbl_l = lv_label_create(alarm_cont);
    lv_label_set_text(lbl_l, "L");
    lv_obj_set_style_text_font(lbl_l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_l, lv_color_hex(0xF39C12), 0);
    lv_obj_add_flag(lbl_l, LV_OBJ_FLAG_HIDDEN);
    ui_manager->alarm_label_l = lbl_l;

    /* LL 下下限标签 - 红色 */
    lv_obj_t *lbl_ll = lv_label_create(alarm_cont);
    lv_label_set_text(lbl_ll, "LL");
    lv_obj_set_style_text_font(lbl_ll, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_ll, lv_color_hex(0xFF3333), 0);
    lv_obj_add_flag(lbl_ll, LV_OBJ_FLAG_HIDDEN);
    ui_manager->alarm_label_ll = lbl_ll;

    /* 右栏：温度（flex_grow=1，与左栏均分剩余空间） */
    lv_obj_t *bottom_right = lv_obj_create(bottom_obj);
    lv_obj_set_size(bottom_right, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(bottom_right, 1);
    ui_container_style_init(bottom_right);
    lv_obj_set_style_bg_opa(bottom_right, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(bottom_right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(bottom_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *bottom_temp_label = lv_label_create(bottom_right);
    lv_label_set_text(bottom_temp_label, "--.-\xC2\xB0""C");
    lv_obj_set_style_text_font(bottom_temp_label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(bottom_temp_label, lv_color_hex(0x2effde), 0);

    lv_subject_add_observer_obj(&ui_manager->subjects.temperature_str,
                                string_label_observer_cb,
                                bottom_temp_label,
                                NULL);
}

/**
 * @brief  创建趋势图瓦片（瞬时流量趋势页面）
 * @details 构建实时瞬时流量趋势图，使用折线图显示流量变化趋势
 *          两条数据序列：10秒采样（短趋势）+ 5分钟采样（长趋势）
 *          底部显示图例和历史最大值
 *
 * @param tile  瓦片对象指针，即趋势图的根容器
 *
 * @par 图表配置
 *        - 图表类型:  折线图（LV_CHART_TYPE_LINE）
 *        - 数据点数:  30个点
 *        - 数据序列1: 蓝色 #3498DB（10秒采样，5分钟历史）
 *        - 数据序列2: 橙色 #F39C12（5分钟采样，150分钟历史）
 *        - Y轴:       动态自适应缩放
 *        - 单位:      m³/h（1 L/s = 3.6 m³/h）
 *
 * @par 布局说明
 *        - 上部: 图表区域（自动填充剩余空间）
 *        - 下部: 图例 + 最大值信息栏（固定高度40px）
 *
 * @see lv_chart_create()
 * @see lv_chart_set_type()
 * @see lv_chart_add_series()
 */
static void create_trend_chart_tile(lv_obj_t *tile)
{
    /*--------------------------------------------------------------------*/
    /* 第一部分：瓦片基础配置                                              */
    /*--------------------------------------------------------------------*/

    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    /*--------------------------------------------------------------------*/
    /* 第二部分：创建图表控件                                              */
    /*--------------------------------------------------------------------*/

    lv_obj_t *chart = lv_chart_create(tile);

    /* 图表位于顶部，底部留40px给信息栏 */
    lv_obj_set_size(chart, LV_PCT(100), lv_pct(83));
    lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 设置图表类型为折线图 */
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);

    /* 每条序列最多30个数据点 */
    lv_chart_set_point_count(chart, 30);

    /* X轴范围（数据点索引 0~29） */
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, 29);

    /* Y轴范围使用4mA/20mA量程（×100缩放） */
    int32_t y_min = (int32_t)(app_config_get_range_4ma() * 100);
    int32_t y_max = (int32_t)(app_config_get_range_20ma() * 100);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);

    /* 仅保留3条水平参考线，去除垂直参考线 */
    lv_chart_set_div_line_count(chart, 3, 0);

    /*--------------------------------------------------------------------*/
    /* 第三部分：添加数据序列                                              */
    /*--------------------------------------------------------------------*/

    /* 10秒采样序列（蓝色，5分钟历史） */
    lv_chart_series_t *ser_10s = lv_chart_add_series(chart,
                                                      lv_color_hex(0x3498DB),
                                                      LV_CHART_AXIS_PRIMARY_Y);

    /* 5分钟采样序列（橙色，150分钟历史） */
    lv_chart_series_t *ser_5min = lv_chart_add_series(chart,
                                                       lv_color_hex(0xF39C12),
                                                       LV_CHART_AXIS_PRIMARY_Y);

    /*--------------------------------------------------------------------*/
    /* 第四部分：图表样式配置                                              */
    /*--------------------------------------------------------------------*/

    lv_obj_set_style_bg_color(chart, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 5, 0);

    /*--------------------------------------------------------------------*/
    /* 第五部分：底部信息栏（图例 + 最大值）                               */
    /*--------------------------------------------------------------------*/

    lv_obj_t *info_bar = lv_obj_create(tile);
    lv_obj_set_size(info_bar, LV_PCT(100), 40);
    lv_obj_align(info_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(info_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_bar, 0, 0);
    lv_obj_set_style_pad_all(info_bar, 5, 0);
    lv_obj_set_flex_flow(info_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 图例1: 蓝色圆点 + "5s" */
    lv_obj_t *dot_5s = lv_obj_create(info_bar);
    lv_obj_set_size(dot_5s, 10, 10);
    lv_obj_set_style_bg_color(dot_5s, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_bg_opa(dot_5s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_5s, 0, 0);
    lv_obj_set_style_radius(dot_5s, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *label_5s = lv_label_create(info_bar);
    lv_label_set_text(label_5s, " 10s");
    lv_obj_set_style_text_color(label_5s, lv_color_hex(0xFFFFFF), 0);

    /* 图例2: 橙色圆点 + "1min" */
    lv_obj_t *dot_60s = lv_obj_create(info_bar);
    lv_obj_set_size(dot_60s, 10, 10);
    lv_obj_set_style_bg_color(dot_60s, lv_color_hex(0xF39C12), 0);
    lv_obj_set_style_bg_opa(dot_60s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_60s, 0, 0);
    lv_obj_set_style_radius(dot_60s, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *label_60s = lv_label_create(info_bar);
    lv_label_set_text(label_60s, " 5min");
    lv_obj_set_style_text_color(label_60s, lv_color_hex(0xFFFFFF), 0);

    /* 最大值标签（右侧对齐，使用flex grow推到右侧） */
    lv_obj_t *spacer = lv_obj_create(info_bar);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t *max_label = lv_label_create(info_bar);
    lv_label_set_text(max_label, "MAX: 0.00 m\xC2\xB3/h");
    lv_obj_set_style_text_color(max_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(max_label, &my_font_montserrat_14, 0);

    /*--------------------------------------------------------------------*/
    /* 第六部分：保存引用到 ui_manager                                     */
    /*--------------------------------------------------------------------*/

    ui_manager->trend_chart = chart;
    ui_manager->trend_series_10s = ser_10s;
    ui_manager->trend_series_5min = ser_5min;
    ui_manager->trend_max_label = max_label;
    ui_manager->trend_tick_counter = 0;
    ui_manager->trend_max_flow = 0.0f;
}

/**
 * @brief  创建累计流量记录瓦片（历史记录页面）
 * @details 构建历史流量记录展示页面，显示当前日期时间、累计运行时长和累计总流量
 *          采用卡片式布局，所有元素严格在 320x240 屏幕内，无需滚动
 *
 * @param tile  瓦片对象指针，即记录页的根容器
 *
 * @par 页面布局 (320x240)
 *        ┌────────────────────────────────┐ y=0
 *        │      2026/03/02 12:30:45      │ 0~30  顶部时间
 *        ├────────────────────────────────┤ y=38
 *        │  ■ RUN TIME                   │ 38~122  卡片1
 *        │    125 day 08:30:15           │        (84px)
 *        ├────────────────────────────────┤ y=126
 *        │  ■ SUM FLOW                   │ 126~210 卡片2
 *        │    1250.8 m3                  │        (84px)
 *        └────────────────────────────────┘ y=210
 *
 * @see lv_label_create()
 * @see lv_subject_add_observer_obj()
 */
static void create_flow_record_tile(lv_obj_t *tile)
{
    /*--------------------------------------------------------------------*/
    /* 瓦片基础配置 - 禁用滚动，禁用布局，纯绝对定位                        */
    /*--------------------------------------------------------------------*/

    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    ui_container_style_init(tile);

    /*--------------------------------------------------------------------*/
    /* 顶部：日期时间 (y=0, h=30)                                         */
    /*--------------------------------------------------------------------*/

    lv_obj_t *time_label = lv_label_create(tile);
    lv_label_set_text(time_label, "2026/03/02 12:30:45");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x2effde), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 5);

    lv_subject_add_observer_obj(&ui_manager->subjects.time_str,
                                string_label_observer_cb,
                                time_label,
                                NULL);

    /*--------------------------------------------------------------------*/
    /* 卡片1：累计运行时长 (y=38, h=84)                                   */
    /*--------------------------------------------------------------------*/

    lv_obj_t *card1 = lv_obj_create(tile);
    lv_obj_set_size(card1, 296, 84);
    lv_obj_align(card1, LV_ALIGN_TOP_LEFT, 12, 38);
    ui_container_style_init(card1);
    lv_obj_set_style_bg_color(card1, lv_color_hex(0x363636), 0);
    lv_obj_set_style_bg_opa(card1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card1, 0, 0);
    lv_obj_set_style_radius(card1, 8, 0);
    lv_obj_set_style_pad_all(card1, 10, 0);
    lv_obj_set_style_pad_left(card1, 18, 0);
    lv_obj_clear_flag(card1, LV_OBJ_FLAG_SCROLLABLE);

    /* 标签 */
    lv_obj_t *record_time_label = lv_label_create(card1);
    lv_label_set_text(record_time_label, "RUN TIME");
    lv_obj_set_style_text_color(record_time_label, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(record_time_label, &lv_font_montserrat_12, 0);
    lv_obj_align(record_time_label, LV_ALIGN_TOP_LEFT, 14, 10);

    /* 左侧蓝色指示条 */
    lv_obj_t *bar1 = lv_obj_create(card1);
    lv_obj_set_size(bar1, 4, 60);
    lv_obj_align(bar1, LV_ALIGN_LEFT_MID, 0, 0);
    ui_container_style_init(bar1);
    lv_obj_set_style_bg_color(bar1, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_bg_opa(bar1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar1, 0, 0);
    lv_obj_set_style_radius(bar1, 2, 0);
    lv_obj_clear_flag(bar1, LV_OBJ_FLAG_SCROLLABLE);

    /* 数值 */
    lv_obj_t *record_time_value = lv_label_create(card1);
    lv_label_set_text(record_time_value, "0 day 00:00:00");
    lv_obj_set_style_text_color(record_time_value, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_text_font(record_time_value, &lv_font_montserrat_24, 0);
    lv_obj_align(record_time_value, LV_ALIGN_TOP_LEFT, 14, 42);

    lv_subject_add_observer_obj(&ui_manager->subjects.total_time_str,
                                string_label_observer_cb,
                                record_time_value,
                                NULL);

    /*--------------------------------------------------------------------*/
    /* 卡片2：累计总流量 (y=126, h=84)                                    */
    /*--------------------------------------------------------------------*/

    lv_obj_t *card2 = lv_obj_create(tile);
    lv_obj_set_size(card2, 296, 84);
    lv_obj_align(card2, LV_ALIGN_TOP_LEFT, 12, 126);
    ui_container_style_init(card2);
    lv_obj_set_style_bg_color(card2, lv_color_hex(0x363636), 0);
    lv_obj_set_style_bg_opa(card2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card2, 0, 0);
    lv_obj_set_style_radius(card2, 8, 0);
    lv_obj_set_style_pad_all(card2, 10, 0);
    lv_obj_set_style_pad_left(card2, 18, 0);
    lv_obj_clear_flag(card2, LV_OBJ_FLAG_SCROLLABLE);

    /* 标签 */
    lv_obj_t *total_flow_label = lv_label_create(card2);
    lv_label_set_text(total_flow_label, "SUM FLOW");
    lv_obj_set_style_text_color(total_flow_label, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(total_flow_label, &lv_font_montserrat_12, 0);
    lv_obj_align(total_flow_label, LV_ALIGN_TOP_LEFT, 14, 10);

    /* 左侧绿色指示条 */
    lv_obj_t *bar2 = lv_obj_create(card2);
    lv_obj_set_size(bar2, 4, 60);
    lv_obj_align(bar2, LV_ALIGN_LEFT_MID, 0, 0);
    ui_container_style_init(bar2);
    lv_obj_set_style_bg_color(bar2, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_style_bg_opa(bar2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar2, 0, 0);
    lv_obj_set_style_radius(bar2, 2, 0);
    lv_obj_clear_flag(bar2, LV_OBJ_FLAG_SCROLLABLE);

    /* 数值 */
    lv_obj_t *total_flow_value = lv_label_create(card2);
    lv_label_set_text(total_flow_value, "0.000 m\xC2\xB3");
    lv_obj_set_style_text_color(total_flow_value, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_style_text_font(total_flow_value, &my_font_montserrat_24, 0);
    lv_obj_align(total_flow_value, LV_ALIGN_TOP_LEFT, 14, 42);

    lv_subject_add_observer_obj(&ui_manager->subjects.total_flow_str,
                                total_flow_with_unit_observer_cb,
                                total_flow_value,
                                NULL);
}

/*============================================================================*/
/*                           对外接口                                           */
/*============================================================================*/

/**
 * @brief  切换当前显示的屏幕
 * @details 执行屏幕切换动画，加载新的屏幕并替换当前屏幕
 *          支持多种切换动画效果，如淡入淡出、滑动等
 * 
 * @param new_screen  新的目标屏幕对象指针
 * @param anim_type   切换动画类型，可选值：
 *                    - LV_SCREEN_LOAD_ANIM_NONE:       无动画
 *                    - LV_SCREEN_LOAD_ANIM_FADE_IN:    淡入
 *                    - LV_SCREEN_LOAD_ANIM_FADE_OUT:   淡出
 *                    - LV_SCREEN_LOAD_ANIM_MOVE_LEFT:  向左滑入
 *                    - LV_SCREEN_LOAD_ANIM_MOVE_RIGHT: 向右滑入
 *                    - LV_SCREEN_LOAD_ANIM_MOVE_TOP:   向上滑入
 *                    - LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM: 向下滑入
 * @param time        动画持续时间（毫秒）
 * 
 * @retval 无
 * 
 * @par 使用示例
 *        ui_switch_screen(settings_screen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 300);
 *        // 切换到设置页面，向左滑入动画，持续300ms
 * 
 * @note  切换前会检查ui_manager和new_screen的有效性
 *        切换后更新ui_manager->active_screen记录当前屏幕
 *        动画完成后旧屏幕会被自动释放
 * 
 * @see lv_screen_load_anim()
 * @see ui_manager_t
 */
void ui_switch_screen(lv_obj_t *new_screen, lv_screen_load_anim_t anim_type, uint32_t time)
{
    /* 检查管理器指针是否有效 */
    if (ui_manager == NULL || new_screen == NULL) return;
    
    /* 执行带动画的屏幕切换 */
    lv_screen_load_anim(new_screen, anim_type, time, 0, true);
    
    /* 更新当前活动屏幕记录 */
    ui_manager->active_screen = new_screen;
}

/**
 * @brief  切换瓦片视图中的当前页面
 * @details 在瓦片视图内切换不同的瓦片页面，支持滑动动画效果
 *          该函数用于实现主页内的多页面导航（如详情页、趋势图、记录页）
 * 
 * @param page_index  目标页面索引
 *                    - 0: 详情页（tile1）- 实时流量监测
 *                    - 1: 趋势图（tile2）- 流量历史趋势
 *                    - 2: 记录页（tile3）- 累计流量记录
 * 
 * @retval 无
 * 
 * @par 瓦片页面对应关系
 *        ┌─────────────────────────────┐
 *        │                             │
 *        │  index=0  →  tile1 (详情)  │
 *        │  index=1  →  tile2 (趋势)  │
 *        │  index=2  →  tile3 (记录)  │
 *        │                             │
 *        └─────────────────────────────┘
 *        通过垂直滑动切换页面
 * 
 * @par 动画效果
 *        页面切换时自动开启滑动动画（LV_ANIM_ON）
 *        动画时长由LVGL默认设置控制
 * 
 * @note  切换前会检查ui_manager和tileview的有效性
 *        切换后更新ui_manager->current_page记录当前页面索引
 * 
 * @see lv_tileview_set_tile()
 * @see create_details_tile()
 * @see create_trend_chart_tile()
 * @see create_flow_record_tile()
 */
void ui_switch_tile(uint8_t page_index)
{
    /* 检查管理器指针和瓦片视图有效性 */
    if (ui_manager == NULL || ui_manager->tileview == NULL) return;

    /* 根据页面索引确定目标瓦片 */
    lv_obj_t *target_tile = NULL;
    switch (page_index) {
        case 0: target_tile = ui_manager->tile1; break;  /* 详情页 */
        case 1: target_tile = ui_manager->tile2; break;  /* 趋势图 */
        case 2: target_tile = ui_manager->tile3; break;  /* 记录页 */
        default: return;  /* 无效索引，直接返回 */
    }

    /* 执行瓦片切换，带滑动动画效果 */
    lv_tileview_set_tile(ui_manager->tileview, target_tile, LV_ANIM_ON);
    
    /* 更新当前页面索引记录 */
    ui_manager->current_page = page_index;
}

/**
 * @brief  刷新趋势图Y轴范围
 * @details 根据当前4mA/20mA量程配置更新趋势图Y轴范围
 *          在量程配置变更时调用以立即生效
 */
void ui_trend_update_range(void)
{
    if (ui_manager == NULL || ui_manager->trend_chart == NULL) return;

    int32_t y_min = (int32_t)(app_config_get_range_4ma() * 100);
    int32_t y_max = (int32_t)(app_config_get_range_20ma() * 100);
    lv_chart_set_axis_range(ui_manager->trend_chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
}

/*============================================================================*/
/*                           内部函数                                           */
/*============================================================================*/

/**
 * @brief  初始化所有Subject（可观察对象）
 * @details 初始化UI模块中使用的所有字符串型Subject对象
 *          每个Subject包含数据缓冲区、前一个数据缓冲区和初始值
 *          Subject是MVVM模式中实现数据绑定的核心组件
 * 
 * @retval 无
 * 
 * @par Subject数据结构
 *        每个字符串Subject由以下三部分组成：
 *        - data_buf:      当前数据缓冲区，存储最新值
 *        - prev_buf:      前一个数据缓冲区，用于比较是否变化
 *        - initial_value: 初始值，初始化时写入缓冲区
 * 
 * @par 初始化的Subject列表
 *        1. time_str           → 当前时间（完整格式：YYYY/MM/DD HH:MM:SS）
 *        2. time_short_str     → 简短时间（HH:MM）
 *        3. total_time_str     → 累计运行时长（N day HH:MM:SS）
 *        4. water_level_str    → 水位高度（L:x.xxxm）
 *        5. instant_flow_str   → 瞬时流量数值
 *        6. total_flow_str     → 累计流量数值
 * 
 * @par 使用流程
 *        1. ui_create() 中调用此函数初始化所有Subject
 *        2. 创建Label控件时，通过lv_subject_add_observer_obj()绑定到Subject
 *        3. 数据更新时，调用lv_subject_copy_string()更新Subject
 *        4. LVGL自动检测Subject变化，触发Observer回调更新UI
 * 
 * @note  此函数必须在创建UI控件之前调用
 *        因为创建控件时需要将Observer绑定到已初始化的Subject
 * 
 * @see lv_subject_init_string()
 * @see lv_subject_add_observer_obj()
 * @see lv_subject_copy_string()
 * @see string_label_observer_cb()
 */
static void ui_init_subjects(void)
{
    /* 初始化时间Subject（完整格式） */
    lv_subject_init_string(&ui_manager->subjects.time_str,
                           ui_manager->subjects.time_buf,
                           ui_manager->subjects.time_prev_buf,
                           sizeof(ui_manager->subjects.time_buf), "");
    
    /* 初始化简短时间Subject */
    lv_subject_init_string(&ui_manager->subjects.time_short_str,
                           ui_manager->subjects.time_short_buf,
                           ui_manager->subjects.time_short_prev_buf,
                           sizeof(ui_manager->subjects.time_short_buf), "");
    
    /* 初始化累计时长Subject */
    lv_subject_init_string(&ui_manager->subjects.total_time_str,
                           ui_manager->subjects.total_time_buf,
                           ui_manager->subjects.total_time_prev_buf,
                           sizeof(ui_manager->subjects.total_time_buf), "");
    
    /* 初始化水位Subject */
    lv_subject_init_string(&ui_manager->subjects.water_level_str,
                           ui_manager->subjects.water_level_buf,
                           ui_manager->subjects.water_level_prev_buf,
                           sizeof(ui_manager->subjects.water_level_buf), "");
    
    /* 初始化累计流量Subject */
    lv_subject_init_string(&ui_manager->subjects.total_flow_str,
                           ui_manager->subjects.total_flow_buf,
                           ui_manager->subjects.total_flow_prev_buf,
                           sizeof(ui_manager->subjects.total_flow_buf), "");
    
    /* 初始化瞬时流量Subject */
    lv_subject_init_string(&ui_manager->subjects.instant_flow_str,
                           ui_manager->subjects.instant_flow_buf,
                           ui_manager->subjects.instant_flow_prev_buf,
                           sizeof(ui_manager->subjects.instant_flow_buf), "");

    /* 初始化4-20mA电流Subject */
    lv_subject_init_string(&ui_manager->subjects.current_ma_str,
                           ui_manager->subjects.current_ma_buf,
                           ui_manager->subjects.current_ma_prev_buf,
                           sizeof(ui_manager->subjects.current_ma_buf), "4.00mA");

    /* 初始化温度Subject */
    lv_subject_init_string(&ui_manager->subjects.temperature_str,
                           ui_manager->subjects.temperature_buf,
                           ui_manager->subjects.temperature_prev_buf,
                           sizeof(ui_manager->subjects.temperature_buf), "--.-\xC2\xB0""C");
}

/**
 * @brief  创建UI界面（系统启动入口函数）
 * @details 初始化LVGL UI系统，创建所有界面元素和数据绑定
 *          该函数是UI模块的初始化入口，在系统启动时调用一次
 * 
 * @retval 无
 * 
 * @par 执行流程
 *        ┌──────────────────────────────────────────────────────┐
 *        │ 1. 初始化数据模型 (app_model_init)                    │
 *        │ 2. 分配UI管理器内存 (lv_malloc_zeroed)               │
 *        │ 3. 初始化所有Subject (ui_init_subjects)               │
 *        │ 4. 创建主屏幕并加载 (init_screen + ui_switch_screen) │
 *        │ 5. 创建瓦片视图和3个页面 (create_xxx_tile)           │
 *        │ 6. 创建其他屏幕预留 (settings/history)               │
 *        │ 7. 创建UI更新定时器 (lv_timer_create)               │
 *        └──────────────────────────────────────────────────────┘
 * 
 * @par 瓦片视图结构
 *        瓦片视图(0,0)为根，包含3个垂直排列的瓦片页面：
 *        ┌─────────────────────────┐
 *        │ tile1 (0,0) - 详情页     │  ← 实时流量监测（初始显示）
 *        │─────────────────────────────────────────────│
 *        │ tile2 (0,1) - 趋势图     │  ← 流量历史趋势
 *        │─────────────────────────────────────────────│
 *        │ tile3 (0,2) - 记录页     │  ← 累计流量记录
 *        └─────────────────────────┘
 *        通过垂直滑动切换页面
 * 
 * @par 定时器配置
 *        UI更新定时器：周期1000ms（1秒）
 *        定时任务：ui_update_timer_cb()
 *        功能：每秒更新数据模型并同步到Subject，触发UI自动刷新
 * 
 * @note  此函数必须先于任何UI操作调用
 *        函数执行时间较长，不应在中断中调用
 *        屏幕页面延迟创建，节省启动时的内存占用
 * 
 * @par 内存管理
 *        ui_manager使用lv_malloc_zeroed分配，会在程序整个生命周期使用
 *        各屏幕对象在切换时由LVGL自动管理
 * 
 * @see app_model_init()
 * @see ui_init_subjects()
 * @see init_screen()
 * @see ui_switch_screen()
 * @see ui_create_tileview()
 * @see create_details_tile()
 * @see create_trend_chart_tile()
 * @see create_flow_record_tile()
 * @see ui_update_timer_cb()
 */
void ui_create(void)
{
    /* 第一步：初始化数据模型，设置初始值 */
    app_model_init();

    /* 第二步：为UI管理器分配内存（零初始化） */
    ui_manager = lv_malloc_zeroed(sizeof(ui_manager_t));
    
    /* 第三步：初始化所有Subject，准备数据绑定 */
    ui_init_subjects();

    /* 第四步：创建并加载主屏幕 */
    ui_manager->main_screen = init_screen();
    ui_switch_screen(ui_manager->main_screen, LV_SCREEN_LOAD_ANIM_NONE, 0);

    /* 第五步：创建瓦片视图容器 */
    ui_manager->tileview = ui_create_tileview(ui_manager->main_screen);
    
    /* 第六步：创建3个瓦片页面（详情、趋势、记录） */
    ui_manager->tile1 = ui_create_tile(ui_manager->tileview, 0, 0, LV_DIR_ALL);
    ui_manager->tile2 = ui_create_tile(ui_manager->tileview, 0, 1, LV_DIR_ALL);
    ui_manager->tile3 = ui_create_tile(ui_manager->tileview, 0, 2, LV_DIR_ALL);
    ui_manager->current_page = 0;

    /* 第七步：填充各瓦片页面内容 */
    create_details_tile(ui_manager->tile1);
    create_trend_chart_tile(ui_manager->tile2);
    create_flow_record_tile(ui_manager->tile3);

    /* 注册配置变更回调，量程变化时立即刷新趋势图 */
    app_config_set_change_callback(ui_config_change_cb);

    /* 初始化传感器状态追踪，首次定时器回调会根据实际状态设置颜色 */
    ui_manager->prev_sensor_online = 1;

    /* 第八步：预留其他屏幕的内存空间（延迟创建） */
    ui_manager->settings_screen = NULL;  /* 设置页面由set_page动态创建 */
    ui_manager->history_screen = init_screen();

    /* 第九步：创建UI更新定时器（每秒刷新一次） */
    lv_timer_create(ui_update_timer_cb, 1000, NULL);
}
