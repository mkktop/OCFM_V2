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

ui_manager_t *ui_manager;

/*============================================================================*/
/*                           私有函数                                           */
/*============================================================================*/

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
    
    /* 第四步：将流量数据同步到Subject */
    lv_subject_copy_string(&ui_manager->subjects.instant_flow_str, g_app_model.instant_flow_str);
    lv_subject_copy_string(&ui_manager->subjects.total_flow_str, g_app_model.total_flow_str);
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
    lv_obj_set_flex_flow(inst_flaw_obj, LV_FLEX_FLOW_COLUMN);

    /* 创建瞬时流量单位标签 */
    lv_obj_t *inst_flaw_label = lv_label_create(inst_flaw_obj);
    lv_obj_set_size(inst_flaw_label, LV_PCT(90), 20);
    lv_label_set_text(inst_flaw_label, "INST:L/min");
    
    /* 设置单位标签字体16px，青绿色 */
    lv_obj_set_style_text_font(inst_flaw_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(inst_flaw_label, lv_color_hex(0x2effde), 0);

    /* 创建瞬时流量数值标签（核心数据显示） */
    lv_obj_t *inst_flaw_data_label = lv_label_create(inst_flaw_obj);
    lv_obj_set_size(inst_flaw_data_label, LV_PCT(90), 50);
    lv_label_set_text(inst_flaw_data_label, "0.00");
    
    /* 设置数值字体为48px最大字号，白色，突出显示 */
    lv_obj_set_style_text_font(inst_flaw_data_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(inst_flaw_data_label, lv_color_hex(0xFFFFFF), 0);
    
    /* 设置文字水平居中对齐 */
    lv_obj_set_style_text_align(inst_flaw_data_label, LV_TEXT_ALIGN_CENTER, 0);
    
    /* 设置数值背景为深灰色 */
    lv_obj_set_style_bg_color(inst_flaw_data_label, lv_color_hex(0x4F4F4F), 0);
    lv_obj_set_style_bg_opa(inst_flaw_data_label, LV_OPA_COVER, 0);
    
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
    lv_obj_set_flex_flow(total_flaw_obj, LV_FLEX_FLOW_COLUMN);

    /* 创建累计流量单位标签 */
    lv_obj_t *total_flaw_label = lv_label_create(total_flaw_obj);
    lv_obj_set_size(total_flaw_label, LV_PCT(90), 20);
    lv_label_set_text(total_flaw_label, "TOTAL:L/min");
    
    /* 设置单位标签字体16px，青绿色 */
    lv_obj_set_style_text_font(total_flaw_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(total_flaw_label, lv_color_hex(0x2effde), 0);

    /* 创建累计流量数值标签 */
    lv_obj_t *total_flaw_data_label = lv_label_create(total_flaw_obj);
    lv_obj_set_size(total_flaw_data_label, LV_PCT(90), 30);
    lv_label_set_text(total_flaw_data_label, "0.00");
    
    /* 设置数值字体为26px，白色 */
    lv_obj_set_style_text_font(total_flaw_data_label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(total_flaw_data_label, lv_color_hex(0xFFFFFF), 0);
    
    /* 设置文字水平居中对齐 */
    lv_obj_set_style_text_align(total_flaw_data_label, LV_TEXT_ALIGN_CENTER, 0);
    
    /* 设置数值背景为深灰色 */
    lv_obj_set_style_bg_color(total_flaw_data_label, lv_color_hex(0x4F4F4F), 0);
    lv_obj_set_style_bg_opa(total_flaw_data_label, LV_OPA_COVER, 0);
    
    /* 绑定累计流量Subject，实现每秒自动更新 */
    lv_subject_add_observer_obj(&ui_manager->subjects.total_flow_str, 
                                string_label_observer_cb, 
                                total_flaw_data_label, 
                                NULL);

    /*--------------------------------------------------------------------*/
    /* 第五部分：底部栏 - 显示4-20mA电流和温度传感器数据                    */
    /*--------------------------------------------------------------------*/
    
    /* 创建底部栏容器 */
    lv_obj_t *bottom_obj = lv_obj_create(tile);
    
    /* 设置底部栏宽度100%，高度50px */
    lv_obj_set_size(bottom_obj, LV_PCT(100), 50);
    ui_container_style_init(bottom_obj);
    
    /* 启用Flex行布局，两个子元素水平排列 */
    lv_obj_set_layout(bottom_obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(bottom_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    /* 设置底部栏背景色 */
    lv_obj_set_style_bg_color(bottom_obj, lv_color_hex(0x1E272E), 0);
    lv_obj_set_flex_flow(bottom_obj, LV_FLEX_FLOW_ROW);

    lv_obj_t *bottom_child1 = lv_obj_create(bottom_obj);
    lv_obj_set_size(bottom_child1, LV_PCT(50), LV_PCT(100));
    ui_container_style_init(bottom_child1);
    lv_obj_set_style_bg_color(bottom_child1, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_border_width(bottom_child1, 0, 0);
    lv_obj_set_layout(bottom_child1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(bottom_child1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *bottom_child1_label = lv_label_create(bottom_child1);
    lv_label_set_text(bottom_child1_label, "16.2ma");
    lv_obj_set_style_text_font(bottom_child1_label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(bottom_child1_label, lv_color_hex(0x2effde), 0);
    lv_obj_set_style_text_align(bottom_child1_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(bottom_child1_label, lv_color_hex(0x8B8B7A), 0);

    lv_obj_t *bottom_child2 = lv_obj_create(bottom_obj);
    lv_obj_set_size(bottom_child2, LV_PCT(50), LV_PCT(100));
    ui_container_style_init(bottom_child2);
    lv_obj_set_style_bg_color(bottom_child2, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_border_width(bottom_child2, 0, 0);
    lv_obj_set_layout(bottom_child2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(bottom_child2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *bottom_child2_label = lv_label_create(bottom_child2);
    lv_label_set_text(bottom_child2_label, "25.2°C");
    lv_obj_set_style_text_font(bottom_child2_label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(bottom_child2_label, lv_color_hex(0x2effde), 0);
    lv_obj_set_style_text_align(bottom_child2_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(bottom_child2_label, lv_color_hex(0x8B8B7A), 0);
}

/**
 * @brief  创建趋势图瓦片（流量趋势页面）
 * @details 构建流量历史趋势展示页面，使用折线图显示流量变化趋势
 *          该页面最多显示2条数据序列，每条序列10个数据点
 * 
 * @param tile  瓦片对象指针，即趋势图的根容器
 * 
 * @par 图表配置
 *        - 图表类型:  折线图（LV_CHART_TYPE_LINE）
 *        - 数据点数:  10个点
 *        - X轴范围:   0-9（索引）
 *        - Y轴范围:   0-100（百分比或相对值）
 *        - 数据序列1: 蓝色（#3498DB）
 *        - 数据序列2: 绿色（#2ECC71）
 * 
 * @par 数据流向
 *        当前为演示数据，实际使用时需要：
 *        1. 在定时器中定时更新数据数组
 *        2. 调用lv_chart_set_array_cnt()更新数据
 *        3. 调用lv_chart_refresh()刷新图表显示
 * 
 * @par 布局说明
 *        - 图表占满整个瓦片区域
 *        - 居中显示，无边距
 *        - 背景色与整体界面一致（深灰蓝）
 * 
 * @note  这是基础版本的趋势图，后续可扩展：
 *        - 添加实时数据更新
 *        - 支持触摸屏缩放
 *        - 添加坐标轴标签
 *        - 支持多条曲线切换显示
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
    
    /* 设置瓦片背景颜色 */
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E272E), 0);
    
    /* 设置背景完全不透明 */
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    /*--------------------------------------------------------------------*/
    /* 第二部分：创建图表并设置基础参数                                    */
    /*--------------------------------------------------------------------*/
    
    /* 创建图表控件 */
    lv_obj_t *chart = lv_chart_create(tile);
    
    /* 设置图表尺寸为100%填充父容器 */
    lv_obj_set_size(chart, LV_PCT(100), LV_PCT(100));
    
    /* 将图表居中放置 */
    lv_obj_center(chart);

    /* 设置图表类型为折线图 */
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    
    /* 设置数据点数量为10个 */
    lv_chart_set_point_count(chart, 10);
    
    /* 设置X轴范围（数据点索引0-9） */
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, 9);
    
    /* 设置Y轴范围（数值0-100） */
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);

    /*--------------------------------------------------------------------*/
    /* 第三部分：添加数据序列                                              */
    /*--------------------------------------------------------------------*/
    
    /* 添加第一条数据序列（蓝色），用于显示瞬时流量 */
    lv_chart_series_t *ser1 = lv_chart_add_series(chart, 
                                                   lv_color_hex(0x3498DB), 
                                                   LV_CHART_AXIS_PRIMARY_Y);
    
    /* 添加第二条数据序列（绿色），用于显示累计流量 */
    lv_chart_series_t *ser2 = lv_chart_add_series(chart, 
                                                   lv_color_hex(0x2ECC71), 
                                                   LV_CHART_AXIS_PRIMARY_Y);

    /*--------------------------------------------------------------------*/
    /* 第四部分：填充演示数据（实际使用时替换为真实数据）                  */
    /*--------------------------------------------------------------------*/
    
    /* 为第一条序列填充10个数据点 */
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

    /* 为第二条序列填充10个数据点 */
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

    /*--------------------------------------------------------------------*/
    /* 第五部分：图表样式配置                                              */
    /*--------------------------------------------------------------------*/
    
    /* 设置图表背景色 */
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x1E272E), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    
    /* 移除图表边框 */
    lv_obj_set_style_border_width(chart, 0, 0);
    
    /* 设置图表内边距为5px */
    lv_obj_set_style_pad_all(chart, 5, 0);
}

/**
 * @brief  创建累计流量记录瓦片（历史记录页面）
 * @details 构建历史流量记录展示页面，显示当前时间、累计运行时长和累计总流量
 *          该页面采用绝对定位布局，元素垂直居中分布
 * 
 * @param tile  瓦片对象指针，即记录页的根容器
 * 
 * @par 页面布局结构（垂直居中排列）
 *        ┌────────────────────────────────┐
 *        │                                │
 *        │      2026/03/02 12:30:45      │  ← 顶部：当前时间（24px）
 *        │           (青绿色)              │
 *        │                                │
 *        │         total time             │  ← 中部上：标签（14px灰色）
 *        │       125 day 08:30:15        │  ← 中部下：累计时长（20px蓝色）
 *        │         (蓝色)                  │
 *        │                                │
 *        │          sum flow              │  ← 中部下：标签（14px灰色）
 *        │         1250.8 m³/h           │  ← 底部：累计流量（24px绿色）
 *        │          (绿色)                 │
 *        │                                │
 *        └────────────────────────────────┘
 * 
 * @par 数据绑定
 *        - time_str         →  顶部时间Label（每秒更新）
 *        - total_time_str   →  中部累计时长Label（每秒更新）
 *        - total_flow_str   →  底部累计流量Label（每秒更新）
 * 
 * @par 样式配置
 *        - 当前时间:  Montserrat 24px, 青绿色(#2effde)
 *        - 标签文字:  Montserrat 14px, 灰色(#BDC3C7)
 *        - 累计时长:  Montserrat 20px, 蓝色(#3498DB)
 *        - 累计流量:  Montserrat 24px, 绿色(#2ECC71)
 * 
 * @note  该页面主要用于展示累计统计数据
 *        布局使用绝对定位，元素间保持固定间距
 * 
 * @see lv_label_create()
 * @see lv_subject_add_observer_obj()
 * @see LV_ALIGN_TOP_MID
 * @see LV_ALIGN_CENTER
 */
static void create_flow_record_tile(lv_obj_t *tile)
{
    /*--------------------------------------------------------------------*/
    /* 第一部分：瓦片基础配置                                              */
    /*--------------------------------------------------------------------*/
    
    /* 设置瓦片背景颜色 */
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1E272E), 0);
    
    /* 设置背景完全不透明 */
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    /*--------------------------------------------------------------------*/
    /* 第二部分：顶部区域 - 显示当前日期时间                                */
    /*--------------------------------------------------------------------*/
    
    /* 创建时间标签 */
    lv_obj_t *time_label = lv_label_create(tile);
    lv_label_set_text(time_label, "2026/03/02 12:30:45");
    
    /* 设置文字颜色为青绿色 */
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x2effde), 0);
    
    /* 设置字体大小为24px */
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
    
    /* 设置文字水平居中对齐 */
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    
    /* 定位到顶部中间位置 */
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 10);
    
    /* 绑定时间Subject，实现每秒自动更新 */
    lv_subject_add_observer_obj(&ui_manager->subjects.time_str, 
                                string_label_observer_cb, 
                                time_label, 
                                NULL);

    /*--------------------------------------------------------------------*/
    /* 第三部分：中部区域 - 显示累计运行时长                                */
    /*--------------------------------------------------------------------*/
    
    /* 创建累计时长标签（单位说明） */
    lv_obj_t *record_time_label = lv_label_create(tile);
    lv_label_set_text(record_time_label, "total time");
    
    /* 设置文字颜色为灰色 */
    lv_obj_set_style_text_color(record_time_label, lv_color_hex(0xBDC3C7), 0);
    
    /* 设置字体大小为14px */
    lv_obj_set_style_text_font(record_time_label, &lv_font_montserrat_14, 0);
    
    /* 设置文字水平居中对齐 */
    lv_obj_set_style_text_align(record_time_label, LV_TEXT_ALIGN_CENTER, 0);
    
    /* 定位到垂直居中位置，向上偏移30px */
    lv_obj_align(record_time_label, LV_ALIGN_CENTER, 0, -30);

    /* 创建累计时长数值标签 */
    lv_obj_t *record_time_value = lv_label_create(tile);
    lv_label_set_text(record_time_value, "125 day 08:30:15");
    
    /* 设置文字颜色为蓝色 */
    lv_obj_set_style_text_color(record_time_value, lv_color_hex(0x3498DB), 0);
    
    /* 设置字体大小为20px */
    lv_obj_set_style_text_font(record_time_value, &lv_font_montserrat_20, 0);
    
    /* 设置文字水平居中对齐 */
    lv_obj_set_style_text_align(record_time_value, LV_TEXT_ALIGN_CENTER, 0);
    
    /* 定位到垂直居中位置 */
    lv_obj_align(record_time_value, LV_ALIGN_CENTER, 0, 0);
    
    /* 绑定累计时长Subject，实现每秒自动更新 */
    lv_subject_add_observer_obj(&ui_manager->subjects.total_time_str, 
                                string_label_observer_cb, 
                                record_time_value, 
                                NULL);

    /*--------------------------------------------------------------------*/
    /* 第四部分：底部区域 - 显示累计总流量                                  */
    /*--------------------------------------------------------------------*/
    
    /* 创建累计流量标签（单位说明） */
    lv_obj_t *total_flow_label = lv_label_create(tile);
    lv_label_set_text(total_flow_label, "sum flow");
    
    /* 设置文字颜色为灰色 */
    lv_obj_set_style_text_color(total_flow_label, lv_color_hex(0xBDC3C7), 0);
    
    /* 设置字体大小为14px */
    lv_obj_set_style_text_font(total_flow_label, &lv_font_montserrat_14, 0);
    
    /* 设置文字水平居中对齐 */
    lv_obj_set_style_text_align(total_flow_label, LV_TEXT_ALIGN_CENTER, 0);
    
    /* 定位到垂直居中位置，向下偏移40px */
    lv_obj_align(total_flow_label, LV_ALIGN_CENTER, 0, 40);

    /* 创建累计流量数值标签 */
    lv_obj_t *total_flow_value = lv_label_create(tile);
    lv_label_set_text(total_flow_value, "1250.8 m3/h");
    
    /* 设置文字颜色为绿色 */
    lv_obj_set_style_text_color(total_flow_value, lv_color_hex(0x2ECC71), 0);
    
    /* 设置字体大小为24px */
    lv_obj_set_style_text_font(total_flow_value, &lv_font_montserrat_24, 0);
    
    /* 设置文字水平居中对齐 */
    lv_obj_set_style_text_align(total_flow_value, LV_TEXT_ALIGN_CENTER, 0);
    
    /* 定位到垂直居中位置，向下偏移70px */
    lv_obj_align(total_flow_value, LV_ALIGN_CENTER, 0, 70);
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

    /* 第八步：预留其他屏幕的内存空间（延迟创建） */
    ui_manager->settings_screen = init_screen();
    ui_manager->history_screen = init_screen();

    /* 第九步：创建UI更新定时器（每秒刷新一次） */
    lv_timer_create(ui_update_timer_cb, 1000, NULL);
}
