# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在此仓库中工作时提供指导。

## 项目概述

OCFM_V2 是一个明渠流量计固件项目，基于 STM32F407VGTx 单片机。通过超声波/雷达水位计测量水位，结合堰槽公式（巴歇尔槽、三角堰、矩形堰）计算瞬时流量和累计流量。

**核心技术栈：**
- 单片机: STM32F407VGTx (Cortex-M4 with FPU, 168MHz)
- 实时系统: FreeRTOS V10.3.1
- 图形界面: LVGL 9.5.0
- 文件系统: FatFs (SD卡, SDIO接口)
- 构建工具: Keil MDK-ARM (AC5工具链) 或 EIDE (VSCode插件)

## 构建命令

### EIDE (VSCode插件)
```bash
# 编译项目
eide build

# 重新编译 (清理 + 编译)
eide rebuild

# 构建配置文件位于 .eide/eide.yml
```

### Keil MDK-ARM
```bash
# 打开工程: MDK-ARM/OCFM_V2.uvprojx
# 编译: Project -> Build Target (F7)
# 下载: Flash -> Download (F8)
```

## 软件架构

### 目录结构
```
OCFM_V2/
├── Core/                    # STM32 HAL和系统文件 (STM32CubeMX生成)
│   ├── Inc/global.h         # 全局配置和寄存器定义
│   └── Src/                 # 外设初始化 (main.c, freertos.c等)
├── App/                     # 应用层代码
│   ├── ui/                  # LVGL UI实现
│   │   ├── ui.c/h           # 主UI逻辑
│   │   ├── ui_conf.h        # UI管理器和Subject定义
│   │   └── ui_set_page.c/h  # 设置页面
│   ├── app_model.c/h        # 数据模型 (MVVM模式)
│   ├── app_log.c/h          # 日志功能
│   ├── app_config.c/h       # 系统配置管理 (EEPROM存储)
│   ├── app_button.c/h       # 应用层按键处理
│   ├── app_sensor.c/h       # 传感器应用层 (Modbus主机封装)
│   └── app_flow_calc.c/h    # 流量计算模块 (堰槽公式)
├── Drivers/
│   ├── STM32F4xx_HAL_Driver/# HAL库
│   ├── CMSIS/               # CMSIS头文件
│   ├── AT24C02/             # EEPROM驱动 (I2C)
│   └── Button/              # 按键驱动 (消抖/长按检测)
├── Interface/               # 通信协议
│   ├── modbus.c/h           # Modbus基础协议
│   ├── modbus_master.c/h    # Modbus主机 (UART1, 水位传感器)
│   └── modbus_slave.c/h     # Modbus从机 (UART2, 用户通信)
├── Middlewares/
│   ├── lvgl/                # LVGL 9.5.0 图形库
│   └── Third_Party/         # FreeRTOS, FatFs
├── FATFS/                   # FatFs配置
└── MDK-ARM/                 # Keil工程文件
```

### 硬件资源分配
| 外设 | 用途 |
|------|------|
| FSMC | LCD驱动 (ST7789, 8080并口) |
| SPI2 | LoRa模块 (SX1278) |
| I2C2 | EEPROM (AT24C02) |
| SDIO | SD卡 |
| UART1 | 水位传感器 (RS485, Modbus主机) |
| UART2 | 用户通信接口 (Modbus从机) |
| UART4 | 4G模块 (ML307) |
| TIM3 | PWM输出 (蜂鸣器/背光) |
| GPIO | 4路继电器 + 4个按键 |

### FreeRTOS任务规划
| 任务 | 优先级 | 周期 | 功能 |
|------|--------|------|------|
| main_task | Normal | 事件驱动 | LVGL界面刷新 (lv_timer_handler) |
| log_task | Low | 5s | 日志输出、RTC时间更新 |
| button_scan_tas | Low | 10ms | 按键扫描 (调用 button_driver_scan) |

## 代码规范

### 格式化
- 缩进: 4个空格 (不使用Tab)
- 大括号风格: Linux风格 (函数开括号在同一行)
- 函数/宏命名: snake_case (下划线命名法)
- 类型命名: CamelCase (驼峰命名法)
- 宏/常量命名: UPPER_SNAKE_CASE (全大写下划线)

### 注释规范
使用Doxygen风格的函数注释：
```c
/**
 * @brief  函数功能简述
 * @param  参数名: 参数说明
 * @retval 返回值说明
 * @note   重要注意事项
 */
```

## LVGL 9.5.0 注意事项

LVGL 9.x 与 8.x API有较大变化：
- 显示刷新回调使用 `lv_display_set_flush_cb()` 而非旧版 `lv_disp_drv_set_flush_cb()`
- 输入设备使用 `lv_indev_create()` 系列API
- 颜色格式在 `lv_conf.h` 中配置
- Observer 模式使用 `lv_subject_t` 和 `lv_observer_t` 实现数据绑定

### 异步UI操作
**重要：** 从按键回调等非LVGL上下文修改UI时，必须使用 `lv_async_call()`，否则会导致 "Asserted at expression: !disp->rendering_in_progress" 错误。

```c
// 错误示例：直接在回调中修改UI
void button_callback(...) {
    ui_switch_tile(next_page);  // 可能触发渲染中修改UI的错误
}

// 正确示例：使用异步调用
static void async_switch_cb(void *context) {
    tile_context_t *ctx = (tile_context_t *)context;
    ui_switch_tile(ctx->page_index);
    lv_free(ctx);
}

void button_callback(...) {
    tile_context_t *ctx = lv_malloc(sizeof(tile_context_t));
    ctx->page_index = next_page;
    lv_async_call(async_switch_cb, ctx);
}
```

## 重要文件

- `Core/Inc/global.h` - 系统配置、寄存器定义、数据结构
- `Core/Src/freertos.c` - FreeRTOS任务定义
- `Middlewares/lvgl/lv_conf.h` - LVGL配置
- `.eide/eide.yml` - EIDE构建配置

## UI 数据更新架构

项目使用 LVGL v9 的观察者模式（Observer Pattern）实现 UI 与数据分离：

```
Model (AppDataModel) → Subject (lv_subject_t) → View (UI控件)
```

**数据流向：**
1. `app_model_update()` 在 LVGL 定时器中调用，从 RTC/传感器获取数据
2. 数据通过 `lv_subject_set_*()` 更新到 Subject
3. 已注册的 Observer 自动通知 UI 控件刷新

**添加新数据字段的步骤：**
1. 在 `App/app_model.h` 的 `AppDataModel` 中添加字段
2. 在 `App/ui/ui_conf.h` 的 `subjects` 中添加对应的 `lv_subject_t`
3. 在 `App/ui/ui.c` 的 `ui_create()` 中初始化 Subject
4. 在 `ui_update_timer_cb()` 中同步数据
5. 使用 `lv_subject_add_observer_obj()` 绑定到 UI 控件

## 按键处理架构

按键系统采用**两层架构**：驱动层 + 应用层

```
button_scan_tas (10ms周期)
    ↓
button_driver_scan() → 状态机处理 (消抖/按下/长按)
    ↓ (松手时触发回调)
app_button_event_handler() → 根据 active_screen 分发
    ↓
app_main_screen_button_handler()
app_set_screen_button_handler()
app_history_screen_button_handler()
```

**按键定义：**
- BUTTON_ID_OK: 确认键
- BUTTON_ID_UP: 上键（主页用于向上翻页）
- BUTTON_ID_DOWN: 下键（主页用于向下翻页）
- BUTTON_ID_SHIFT: 位移键

**事件类型：**
- BUTTON_EVENT_SHORT: 短按 (<2秒)
- BUTTON_EVENT_LONG: 长按 (>=2秒)

**添加新页面按键处理的步骤：**
1. 在 `App/app_button.c` 中添加 `app_xxx_screen_button_handler()` 函数
2. 在 `app_button_event_handler()` 中添加页面判断分支
3. 在 `App/app_button.h` 中添加函数声明
4. 在 `ui_conf.h` 的 `ui_manager_t` 中确保有对应的屏幕指针

## 系统配置管理 (app_config)

`App/app_config.c/h` 提供系统参数的持久化存储管理，使用 EEPROM (AT24C02) 存储配置。

**核心功能：**
- `app_config_init()` - 初始化配置，从EEPROM加载，无效则使用默认值
- `app_config_save()` - 保存配置到EEPROM
- `app_config_load()` - 从EEPROM加载配置
- `app_config_get()` - 获取配置结构体指针
- `app_config_factory_reset()` - 恢复出厂设置

**配置字段类别：**
- 基本参数：range_max, height, calibration_4ma/20ma, point_num
- 测量参数：window_width, filter_count, delay_time, antenna_type, blind_area
- Modbus参数：modbusAddr, modbusBaudRate, modbusStopBits
- 报警参数：alarm_ah/al, alarm_dh/dl, alarm_aah/aal
- 其他：canals_type, channel_id, instant_unit, language

**注意：** 所有配置修改后需调用 `app_config_save()` 才能持久化。

## 传感器应用层 (app_sensor)

`App/app_sensor.c/h` 封装了水位传感器的Modbus主机通信，提供简洁的应用层接口。

**架构层次：**
```
app_sensor (应用层)
    ↓
modbus_master (协议层)
    ↓
UART1 + DMA (硬件层)
```

**核心API：**
- `app_sensor_init()` - 初始化传感器模块（在系统启动时调用）
- `app_sensor_poll()` - 轮询任务（需在FreeRTOS任务中周期性调用，建议10ms）
- `app_sensor_get_distance()` - 获取距离值 (m)
- `app_sensor_is_online()` - 检查传感器在线状态
- `app_sensor_get_data()` - 获取完整传感器数据结构

**水位计算公式：**
```
水位 = 安装高度 - 距离
```
安装高度从 `app_config_get_height()` 获取，单位mm。

## 流量计算模块 (app_flow_calc)

`App/app_flow_calc.c/h` 实现流量计算功能，基于堰槽公式（巴歇尔槽、三角堰、矩形堰）计算瞬时流量和累计流量。

**核心API：**
- `flow_calc_update()` - 更新流量计算（每秒调用一次）
- `flow_calc_get_instant()` - 获取当前瞬时流量
- `flow_calc_get_total()` - 获取累计流量 (m³)
- `flow_calc_reset_total()` - 清零累计流量
- `flow_calc_load_total()` - 从EEPROM备份加载累计流量
- `flow_calc_save_total()` - 保存累计流量到EEPROM备份
- `flow_calc_process()` - 处理EEPROM保存请求（在主循环调用，避免定时器中阻塞）

**水渠类型：**
- PARSHALL_FLUME (1) - 巴歇尔水槽
- TRIANGULAR_WEIR (2) - 三角堰
- RECTANGULAR_WEIR (3) - 矩形堰

**累计流量存储：**
- 存储地址：`TOTAL_FLOW_EEPROM_ADDR` (240)
- 使用 `TOTAL_FLOW_MAGIC_NUMBER` (0x5A5A5A5AU) 作为校验
- 每次重大变更时调用 `flow_calc_save_total()` 备份

## Modbus寄存器映射

主要保持寄存器 (完整列表见 Core/Inc/global.h)：
- 0x0001: 水位 (uint16)
- 0x0002: 距离 (uint16)
- 0x0003: 温度 (uint16)
- 0x0004-0x0005: 瞬时流量 (float, 占2个寄存器)
- 0x0006-0x0009: 累计流量 (double, 占4个寄存器)
- 0x000A-0x000D: 继电器状态 (各占1个uint16)

## UI页面结构

### 主屏幕瓦片视图 (Tileview)
主屏幕使用垂直瓦片视图布局，包含3个瓦片页：
- tile1 (0,0): 详情页（首页）
- tile2 (0,1): 趋势图页
- tile3 (0,2): 累计流量记录页

**瓦片切换：** 使用 `ui_switch_tile(page_index)` 函数，page_index 为 0/1/2

### 屏幕切换
使用 `ui_switch_screen(new_screen, anim_type, time)` 切换屏幕：
- `ui_manager->main_screen`: 主屏幕（瓦片视图）
- `ui_manager->settings_screen`: 设置屏幕
- `ui_manager->history_screen`: 历史记录屏幕
- `ui_manager->active_screen`: 当前激活屏幕（用于按键事件分发）
