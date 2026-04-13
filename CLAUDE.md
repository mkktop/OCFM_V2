# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

OCFM_V2 是一个明渠流量计固件项目，基于 STM32F407VGTx 单片机。通过超声波/雷达水位计测量水位，结合堰槽公式（巴歇尔槽、三角堰、矩形堰）计算瞬时流量和累计流量。

**核心技术栈：**
- 单片机: STM32F407VGTx (Cortex-M4 with FPU, 168MHz)
- 实时系统: FreeRTOS V10.3.1
- 图形界面: LVGL 9.5.0
- 文件系统: FatFs (SD卡, SDIO接口)
- 构建工具: Keil MDK-ARM (AC5工具链) 或 EIDE (VSCode插件)

### 构建
**Keil MDK-ARM：**
```
打开工程: MDK-ARM/OCFM_V2.uvprojx
编译: Project -> Build Target (F7)
下载: Flash -> Download (F8)
```

**EIDE (VSCode插件)：**
```
构建输出: build/ 目录
工具链: AC5 (ARM Compiler v5), 使用 microLIB, Debug模式, 优化等级0
烧录: J-Link
配置文件: .eide/eide.yml (注意: .eide/ 在.gitignore中，需手动维护)
```

## 软件架构

### 目录结构
```
OCFM_V2/
├── Core/                    # STM32 HAL和系统文件 (STM32CubeMX生成)
│   ├── Inc/
│   │   ├── global.h         # 全局配置、寄存器定义、SystemConfig_t
│   │   └── rtc_time.h       # 统一RTC时间API (格式化、时间戳)
│   └── Src/                 # 外设初始化 (main.c, freertos.c, rtc.c等)
├── App/                     # 应用层代码
│   ├── ui/                  # LVGL UI实现
│   │   ├── ui.c/h           # 主UI逻辑 (Tileview, Observer绑定, 定时器)
│   │   ├── ui_conf.h        # ui_manager_t和Subject定义
│   │   ├── ui_set_page.c/h  # 三级设置菜单 (分类→参数→编辑)
│   │   ├── ui_async.c/h     # [已弃用] 仅为Keil工程兼容保留，使用lv_async_call()替代
│   │   └── font/            # 自定义字体 (支持m³等特殊符号)
│   ├── app_model.c/h        # 数据模型 (MVVM, AppDataModel)
│   ├── app_log.c/h          # 日志初始化
│   ├── app_config.c/h       # 系统配置管理 (EEPROM存储, getter/setter)
│   ├── app_button.c/h       # 应用层按键处理 (按屏幕分发)
│   ├── app_sensor.c/h       # 传感器应用层 (Modbus主机封装)
│   ├── app_flow_calc.c/h    # 流量计算模块 (堰槽公式, 累计流量)
│   ├── app_alarm.c/h        # 流量报警模块 (4级滞回控制, 4路继电器输出)
│   ├── app_current.c/h      # 4-20mA模拟电流输出 (TIM3 CH4 PWM→V/I)
│   └── app_modbus_slave.c/h # Modbus从机应用层 (寄存器映射, 写回调)
├── Drivers/
│   ├── STM32F4xx_HAL_Driver/# HAL库
│   ├── CMSIS/               # CMSIS头文件
│   ├── AT24C02/             # EEPROM驱动 (I2C)
│   ├── Button/              # 按键驱动 (消抖/长按检测)
│   ├── CT1820/              # CT1820温度传感器驱动 (1-Wire, PB0)
│   └── File/                # 文件系统抽象层
│       ├── file_driver.c/h  # FATFS封装 (单文件操作)
│       ├── data_recorder.c/h# CSV历史数据记录 (/data/YYYY/MM/DD.csv)
│       └── log_manager.c/h  # 系统日志管理 (/LOGS/SYS|USER|ALARM/)
├── Interface/               # 通信协议
│   ├── modbus.c/h           # Modbus基础协议
│   ├── modbus_master.c/h    # Modbus主机 (UART1, 水位传感器)
│   └── modbus_slave.c/h     # Modbus从机 (UART2, 用户通信)
├── Middlewares/
│   ├── lvgl/                # LVGL 9.5.0 图形库
│   └── Third_Party/         # FreeRTOS, FatFs
├── FATFS/                   # FatFs配置 (STM32CubeMX生成)
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
| TIM3 | PWM输出 (蜂鸣器/背光) + 4-20mA电流输出 (CH4/PB1) |
| GPIO | 4路继电器 (PA4-PA7) + 4个按键 |
| GPIO PB0 | CT1820温度传感器 (1-Wire) |

### FreeRTOS任务与定时器
| 任务 | 栈大小 | 优先级 | 周期 | 功能 |
|------|--------|--------|------|------|
| main_task | 8192 | Normal | 事件驱动 | LVGL界面刷新 + flow_calc_process() |
| log_task | 2048 | Low | 5s | 日志输出、RTC时间更新 |
| button_scan_tas | 2048 | Low | 10ms | 按键扫描 (button_driver_scan) |
| modbus_master_t | 1024 | Low | 10ms | 传感器轮询 (app_sensor_poll) |
| modbus_slave_ta | 1024 | Low | 10ms | 从机通信 + app_modbus_slave_update() (1s) |

| 定时器 | 周期 | 回调 |
|--------|------|------|
| flow_refresh_timer | 1000ms | flow_calc_update() |

### 系统启动流程 (main_task_func)
```
app_config_init()          → 从EEPROM加载配置
flow_calc_load_total()     → 从备份寄存器/EEPROM加载累计流量
app_alarm_init()           → 初始化报警模块 (继电器默认关闭)
app_current_init()         → 初始化4-20mA输出 (TIM3 CH4 PWM)
app_sensor_init()          → 初始化传感器模块
lv_init() + display port   → 初始化LVGL显示
ui_create()                → 创建完整UI
循环: flow_calc_process() + lv_timer_handler()
```

## 代码规范

- 缩进: 4个空格 (不使用Tab)
- 大括号风格: Linux风格 (函数开括号在同一行)
- 函数/宏命名: snake_case | 类型命名: CamelCase | 宏/常量: UPPER_SNAKE_CASE
- 注释使用Doxygen风格: `@brief`, `@param`, `@retval`, `@note`

## LVGL 9.5.0 注意事项

LVGL 9.x 与 8.x API有较大变化：
- 显示刷新: `lv_display_set_flush_cb()` (非旧版 `lv_disp_drv_set_flush_cb()`)
- 输入设备: `lv_indev_create()` 系列API
- 数据绑定: `lv_subject_t` / `lv_observer_t` 实现Observer模式

**LVGL配置 (lv_conf.h) 关键参数：**
- 内存池: 64KB，位于外部SRAM (FSMC地址 0x10000000)
- 颜色深度: 16位 (RGB565)
- OS集成: `LV_OS_FREERTOS`，使用任务通知
- 默认字体: `lv_font_montserrat_14`，另有自定义字体 `lv_font_sup3_14/16/24`（支持m³等特殊符号，源文件在 `App/ui/font/`）
- 编码: UTF-8
- 刷新周期: 33ms (~30 FPS)

**注意：** `/Middlewares` 目录（LVGL、FreeRTOS、FatFs）在 `.gitignore` 中被排除，不会被git跟踪。

### 异步UI操作
**从按键回调等非LVGL上下文修改UI时，必须使用 `lv_async_call()`**，否则会触发 "Asserted at expression: !disp->rendering_in_progress"。

```c
// 错误：直接在回调中修改UI
void button_callback(...) {
    ui_switch_tile(next_page);  // 可能崩溃
}

// 正确：使用异步调用
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

- `Core/Inc/global.h` - 全局配置、Modbus寄存器定义、SystemConfig_t
- `Core/Src/freertos.c` - FreeRTOS任务/定时器定义和启动流程
- `Core/Inc/rtc_time.h` / `Core/Src/rtc_time.c` - 统一RTC时间API
- `Middlewares/lvgl/lv_conf.h` - LVGL配置
- `.eide/eide.yml` - EIDE构建配置

## UI 数据更新架构 (MVVM)

```
Model (AppDataModel) → Subject (lv_subject_t) → View (UI控件)
```

**数据流向：**
1. `app_model_update()` 在 LVGL 定时器中调用，从 RTC/传感器/流量模块获取数据
2. 数据通过 `lv_subject_set_*()` 更新到 Subject
3. 已注册的 Observer 自动通知 UI 控件刷新

**AppDataModel 关键字段：** time_str, time_short_str, total_time_str, water_level_str, instant_flow_str, total_flow_str, instant_flow, total_flow, water_level_m, sensor_online

**添加新数据字段的步骤：**
1. 在 `App/app_model.h` 的 `AppDataModel` 中添加字段
2. 在 `App/ui/ui_conf.h` 的 `subjects` 中添加对应的 `lv_subject_t`
3. 在 `App/ui/ui.c` 的 `ui_create()` 中初始化 Subject
4. 在 `ui_update_timer_cb()` 中同步数据
5. 使用 `lv_subject_add_observer_obj()` 绑定到 UI 控件

## 按键处理架构

```
button_scan_tas (10ms周期)
    ↓
button_driver_scan() → 状态机处理 (消抖/按下/长按)
    ↓ (松手时触发回调)
app_button_event_handler() → 根据 active_screen 分发
    ↓
app_main_screen_button_handler() / app_set_screen_button_handler() / app_history_screen_button_handler()
```

**按键：** BUTTON_ID_OK (确认), BUTTON_ID_UP (上), BUTTON_ID_DOWN (下), BUTTON_ID_SHIFT (位移)
**事件：** BUTTON_EVENT_SHORT (<2秒), BUTTON_EVENT_LONG (>=2秒, 到达阈值立即触发)

**添加新页面按键处理：**
1. 在 `App/app_button.c` 中添加 `app_xxx_screen_button_handler()`
2. 在 `app_button_event_handler()` 中添加页面判断分支
3. 在 `App/app_button.h` 中添加函数声明
4. 在 `ui_conf.h` 的 `ui_manager_t` 中确保有对应的屏幕指针

## 系统配置管理 (app_config)

使用 EEPROM (AT24C02) 持久化存储，配置结构体 `SystemConfig_t` 定义在 `global.h`。

**核心API：** `app_config_init()`, `app_config_save()`, `app_config_load()`, `app_config_get()`, `app_config_factory_reset()`
**参数访问：** 每个配置项都有独立的 getter/setter，如 `app_config_get_height()` / `app_config_set_height()`

**配置字段类别：**
- 基本参数：range_max, height, calibration_4ma/20ma, point_num, range_4ma/20ma
- 测量参数：window_width, filter_count, delay_time, antenna_type, blind_area, w_coeff, m_coeff
- Modbus参数：modbus_addr, modbus_baudrate, modbus_stopbits
- 报警参数：alarm_ah/al, alarm_dh/dl, alarm_aah/aal
- 其他：canals_type, channel_id, instant_unit, language, factory_settings, dis_offset, sum_point

**注意：** 所有配置修改后需调用 `app_config_save()` 才能持久化。Modbus从机写回参数时使用脏标记 + 3秒延迟保存机制。

## 传感器应用层 (app_sensor)

```
app_sensor (应用层, App/app_sensor.c)
    ↓
modbus_master (协议层, Interface/modbus_master.c)
    ↓
UART1 + DMA (硬件层)
```

**核心API：**
- `app_sensor_init()` - 初始化（启动时调用）
- `app_sensor_poll()` - 轮询（FreeRTOS任务中10ms周期调用）
- `app_sensor_get_distance()` / `app_sensor_get_data()` - 获取距离/完整数据
- `app_sensor_is_online()` - 传感器在线状态

**传感器参数设置（异步非阻塞）：** `app_sensor_set_height()`, `app_sensor_set_range()`, `app_sensor_set_float()` 等，返回命令索引，结果通过回调或 `app_sensor_get_cmd_status()` 获取。

**水位计算：** `水位 = 安装高度 - 距离`，安装高度从 `app_config_get_height()` 获取 (mm)。

## 流量计算模块 (app_flow_calc)

**核心API：**
- `flow_calc_update()` - 更新流量（每秒由定时器调用）
- `flow_calc_get_instant()` / `flow_calc_get_total()` - 获取瞬时流量/累计流量
- `flow_calc_get_total_time()` - 获取累计运行时间 (秒)
- `flow_calc_reset_total()` - 清零累计流量
- `flow_calc_process()` - 处理延迟EEPROM保存（主循环中调用）

**水渠类型与公式：**
- PARSHALL_FLUME (1): Q = K * H^n（16种标准喉部宽度）
- TRIANGULAR_WEIR (2): Q = K * H^2.5（5种标准角度: 90/60/45/30/22.5度）
- RECTANGULAR_WEIR (3): Q = K * b * H^1.5（4种标准宽度: 0.5/1.0/1.5/2.0m）

**流量单位：** L/s, L/min, L/h, m³/h, m³/s, m³/min, T/h, G/h（由 `instant_unit` 配置决定）

**累计流量双层持久化：**
- RTC备份寄存器：每10秒保存
- EEPROM AT24C02：每5分钟保存（地址 `TOTAL_FLOW_EEPROM_ADDR`=240）
- 校验魔数：`TOTAL_FLOW_MAGIC_NUMBER` (0x5A5A5A5AU)

## Modbus从机应用层 (app_modbus_slave)

桥接 Modbus 协议层与应用业务逻辑，管理保持寄存器的读写。

**核心API：**
- `app_modbus_slave_init()` - 启动时预填配置参数到寄存器
- `app_modbus_slave_update()` - 每秒同步实时数据到寄存器（水位、距离、温度、流量、继电器、报警值、传感器参数、RTC时间）
- `app_modbus_slave_on_write()` - 处理主机写请求（配置变更、出厂复位、清零流量、RTC时间设置）
- `app_modbus_slave_process()` - 处理延迟EEPROM保存

## 报警模块 (app_alarm)

4级流量报警控制，驱动4路继电器输出。由 `flow_calc_update()` 每秒调用。

**报警类型与继电器映射：**
| 类型 | 继电器 | GPIO | 触发条件 | 恢复条件 |
|------|--------|------|----------|----------|
| AH (上限) | relay1 | PA4 | 流量 > AH | 流量 < AH - DH |
| AL (下限) | relay2 | PA5 | 流量 < AL | 流量 > AL + DL |
| AAH (上上限) | relay3 | PA6 | 流量 > AAH | 流量 < AH |
| AAL (下下限) | relay4 | PA7 | 流量 < AAL | 流量 > AL |

**核心API：**
- `app_alarm_init()` - 初始化（继电器默认关闭）
- `app_alarm_update(float flow_m3h)` - 更新报警状态（流量单位: m³/h，传感器离线传0.0f）
- `app_alarm_get_state(type)` - 获取指定报警类型状态 (NORMAL/ACTIVE)
- `app_alarm_get_relay_states()` - 获取4路继电器状态位图
- `app_alarm_set_relay(relay, state)` - 手动控制继电器（调试用）

**滞回逻辑：** AH触发后，需流量降到AH-DH以下才恢复；AAH触发后，需流量降到AH以下才恢复（非AAH-DH）。下限同理。

## 4-20mA电流输出 (app_current)

将瞬时流量线性映射为PWM占空比，驱动外部V/I电路产生4-20mA标准工业模拟信号。

**硬件链路：** TIM3 CH4 (PB1) → RC低通滤波 → V/I转换器 → 4-20mA输出

**核心API：**
- `app_current_init()` - 启动TIM3 CH4 PWM，初始输出4mA
- `app_current_update(float flow_data)` - 根据流量(m³/h)更新CCR寄存器
- `app_current_calc_ma(float flow_data)` - 计算电流值（返回百分之一mA，如1200=12.00mA）
- `app_current_format_ma(flow_data, buf, size)` - 格式化为字符串（如"12.00mA"）

**校准模型：** `calibration_4ma/20ma`为工厂校准的CCR值，`range_4ma/20ma`为用户可调的流量量程端点。

## Modbus寄存器映射

完整列表见 `Core/Inc/global.h`，主要寄存器：

| 地址 | 名称 | 类型 | 说明 |
|------|------|------|------|
| 0x0001 | REG_WUWEI | uint16 | 水位 |
| 0x0002 | REG_DISTANCE | uint16 | 距离 |
| 0x0003 | REG_TEMPERATURE | uint16 | 温度 |
| 0x0004-0x0005 | REG_INSTANT_FLOW | float | 瞬时流量 (2寄存器) |
| 0x0006-0x0009 | REG_SUM_FLOW | double | 累计流量 (4寄存器) |
| 0x000A-0x000D | REG_RELAY1-4_STATUS | uint16×4 | 继电器状态 |
| 0x000E-0x0018 | REG_AH~REG_AAL | uint32×6 | 报警值 (各占2寄存器) |
| 0x0065-0x006F | 传感器参数 | uint16 | range_max, height, calibration等 |
| 0x0101-0x0107 | 从机参数 | uint16 | canals_type, channel_id, instant_unit等 |
| 0x0200-0x0206 | RTC时间 | uint16 | year, month, day, hour, min, sec, weekday |

## RTC时间模块 (rtc_time)

统一的RTC时间接口，封装STM32 HAL RTC外设。

**核心API：**
- `RTC_Time_Init()` - 初始化（在 `MX_RTC_Init()` 之后调用）
- `RTC_Time_Get()` / `RTC_Time_Set()` - 读写时间（`RTC_TimeData` 结构体）
- `RTC_Time_GetString(buffer, format)` - 格式化时间字符串
- `RTC_Time_GetTimestamp()` - Unix时间戳
- `RTC_Time_IsValid()` - 检查RTC是否处于默认状态

**格式类型：** DATE(`YYYY-MM-DD`), TIME(`HH:MM:SS`), FULL, COMPACT, LOG_FILE, LOG_DATETIME

**全局变量：** `extern RTC_TimeData g_RtcTime`（rtc_time.c中定义）

## 文件系统抽象层 (Drivers/File)

### file_driver - FATFS封装
单文件操作驱动（同一时间只能打开一个文件）。

**核心API：** `file_init()`, `file_open(path, mode)`, `file_close()`, `file_read()`, `file_write()`, `file_delete()`, `file_exists()`, `file_create_dir()`
**文件模式：** FILE_MODE_READ(0), FILE_MODE_WRITE(1), FILE_MODE_APPEND(2), FILE_MODE_OPEN(3)
**状态码：** FILE_OK, FILE_ERROR, FILE_NOT_MOUNTED, FILE_NOT_OPENED, FILE_READ_ERROR, FILE_WRITE_ERROR

### data_recorder - CSV历史数据记录
按日期分目录存储：`/data/YYYY/MM/DD.csv`

**记录结构 (DataRecord)：** timestamp, water_level, instant_flow, total_flow, temperature, flags, total_time
**核心API：**
- `data_recorder_init(config)` / `data_record()` / `data_record_flow()` - 初始化与记录
- `data_query()` / `data_query_by_date()` - 时间范围查询（回调模式）
- `data_get_statistics()` - 聚合统计（平均/最大/最小流量等）
- `data_cleanup()` / `data_delete_before()` - 数据保留管理（默认365天）
- `data_export()` - 导出为CSV或JSON格式

### log_manager - 系统日志管理
三类日志按日期分目录存储：`/LOGS/SYS/`, `/LOGS/USER/`, `/LOGS/ALARM/`

**日志类型：** LOG_TYPE_SYSTEM(0), LOG_TYPE_USER(1), LOG_TYPE_ALARM(2)
**记录结构 (LogRecord)：** timestamp + content (最大128字符)
**核心API：**
- `log_manager_init(config)` / `log_write(type, content)` - 初始化与写入
- `log_query()` / `log_query_by_date()` - 查询
- `log_cleanup()` / `log_cleanup_all()` - 自动清理（默认30天，最大90天）
- `log_format()` / `log_format_all()` - 清空日志

## CT1820温度传感器驱动 (Drivers/CT1820)

1-Wire协议位操作驱动，使用PB0引脚。

**核心API：**
- `CT1820_Init()` - 初始化
- `CT1820_StartConvert()` - 启动转换（非阻塞，需等待>=750ms）
- `CT1820_GetTemp()` - 读取温度（返回值×10，如256=25.6°C，有约2ms忙等待）

## UI页面结构

### 主屏幕瓦片视图 (Tileview)
主屏幕使用垂直瓦片视图布局，包含3个瓦片页：
- tile1 (0,0): 详情页 - 顶栏(时间+水位), 瞬时流量(48px), 累计流量(26px), 底栏(4-20mA+温度)
- tile2 (0,1): 趋势图页 - 双系列折线图(蓝=5s采样, 橙=60s采样), 60数据点, Y轴自动缩放
- tile3 (0,2): 累计流量记录页 - 日期时间, 累计运行时长, 累计总流量

**瓦片切换：** `ui_switch_tile(page_index)` (0/1/2)

### 设置页面 (三级菜单)
- Level 1: 分类列表 (基本/测量/Modbus/报警/系统/时间)
- Level 2: 参数列表 (显示当前值)
- Level 3: 编辑页 (全屏编辑, 红色高亮当前位, SHIFT键切换步进: 1/10/100/1000/10000)
- 15秒无操作自动返回主屏幕

### 屏幕切换
`ui_switch_screen(new_screen, anim_type, time)`:
- `ui_manager->main_screen`: 主屏幕（瓦片视图）
- `ui_manager->settings_screen`: 设置屏幕
- `ui_manager->history_screen`: 历史记录屏幕
- `ui_manager->active_screen`: 当前激活屏幕（按键事件分发依据）
