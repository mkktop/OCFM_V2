# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

OCFM_V2 是一个明渠流量计固件项目，基于 STM32F407VGTx 单片机。通过超声波/雷达水位计测量水位，结合堰槽公式（巴歇尔槽、三角堰、矩形堰）计算瞬时流量和累计流量。

**核心技术栈：** STM32F407VGTx (Cortex-M4, 168MHz) | FreeRTOS V10.3.1 | LVGL 9.5.0 | FatFs | Keil MDK-ARM (AC5)

**关键约束：**
- EEPROM AT24C02 仅 256 字节，`SystemConfig_t` 约 148 字节（27×uint32 + 10×float，定义在 `Core/Inc/global.h`）。累计流量存储在地址 232（24字节）。添加新配置字段前务必确认剩余空间（配置区 0~147，累计流量区 232~255，中间 148~231 字节空闲）。
- `Core/Inc/global.h` 是项目的**总头文件**——include 了几乎所有模块头文件（at24c02、fatfs、file_driver、data_recorder、log_manager、rtc_time、ui 等），并集中定义 Modbus 寄存器地址、系统常量、`SystemConfig_t` 结构体。绝大多数 `.c` 文件只需 `#include "global.h"` 即可获得所有依赖。
- 无自动化测试和lint工具，验证依赖实机调试和串口日志输出。

### 构建
```
打开工程: MDK-ARM/OCFM_V2.uvprojx
编译: Project -> Build Target (F7)
下载: Flash -> Download (F8)
```

**硬件配置修改：** 外设引脚、时钟树等硬件配置通过 CubeMX 打开 `OCFM_V2.ioc` 修改，然后重新生成代码。不要手动修改 HAL 初始化代码（`Core/Src/main.c`、`stm32f4xx_hal_msp.c` 等）中的外设配置。

## 硬件资源分配

| 外设 | 用途 |
|------|------|
| FSMC | LCD驱动 (ST7789, 8080并口) |
| SPI2 | LoRa模块 (SX1278) |
| I2C2 | EEPROM (AT24C02, 256字节) |
| SDIO | SD卡 |
| UART1 | 水位传感器 (RS485, Modbus主机) |
| UART2 | 用户通信接口 (Modbus从机) |
| UART4 | 4G模块 (ML307) |
| TIM3 | PWM (蜂鸣器/背光) + 4-20mA电流输出 (CH4/PB1) |
| GPIO PA4-PA7 | 4路继电器 (AH/AL/AAH/AAL) |
| GPIO PB0 | CT1820温度传感器 (1-Wire) |

## FreeRTOS 任务与启动流程

| 任务 | 栈(字节) | 周期 | 功能 |
|------|----------|------|------|
| main_task | 8192 | 事件驱动 | LVGL刷新 + 启动时初始化所有模块 |
| log_task | 8192 | 500ms | 日志输出、config_process、flow_process、历史查询、RTC更新 |
| button_scan_tas | 2048 | 10ms | 按键扫描 |
| modbus_master_t | 1024 | 10ms | 传感器轮询 |
| modbus_slave_ta | 1024 | 10ms | 从机通信 (每200ms同步数据到寄存器) |

| 定时器 | 周期 | 回调 |
|--------|------|------|
| flow_refresh_timer | 1000ms | 读取水位 → flow_calc_update() → app_alarm_update() → app_current_update() |

**独立看门狗 (IWDG):** 预分频64，重载值4095，超时≈8.2秒。在 main_task 初始化完成后和 log_task 每500ms循环中刷新。其他任务（button_scan、modbus_master、modbus_slave）通过 `app_system_wait_ready()` 等待 main_task 初始化完成后才启动。

**系统启动顺序** (main_task_func, `Core/Src/freertos.c`)：
1. `app_config_init()` — 从EEPROM加载配置
2. `app_alarm_init()` — 继电器默认关闭
3. `flow_calc_load_total()` — 从备份寄存器/EEPROM加载累计流量
4. `app_sensor_init()` — 初始化传感器（参数在传感器首次上线时同步，非启动时）
5. `app_current_init()` — 启动TIM3 CH4 PWM，初始输出4mA
6. `lv_init()` + `lv_tick_set_cb()` + `lv_delay_set_cb()` + `lv_port_disp_init()` + `ui_create()` — 初始化LVGL和UI
7. `app_system_set_ready()` — 通知其他任务可以启动
8. 启动 `flow_refresh_timer` (1秒周期) + 刷新IWDG
9. 预渲染10帧后开背光，进入 `lv_timer_handler()` 主循环

## 代码规范

- 格式化配置: `.clang-format` (基于 Microsoft 风格, 4空格缩进, Linux大括号, 无行长度限制, 允许单行if/for)
- 命名: snake_case函数 / CamelCase类型 / UPPER_SNAKE_CASE宏常量
- 注释: Doxygen风格 (`@brief`, `@param`, `@retval`, `@note`)

## LVGL 9.5.0 注意事项

LVGL 9.x API 与 8.x 差异较大，本项目的关键API：
- 显示: `lv_display_set_flush_cb()` / 数据绑定: `lv_subject_t` + `lv_observer_t` / OS: `LV_OS_FREERTOS`

**LVGL配置** (`Middlewares/lvgl/lv_conf.h`)：内存池 64KB (外部SRAM 0x10000000), RGB565, 刷新周期 20ms

**自定义字体** (`App/ui/font/`)：
- `lv_font_sup3_14/16/24` — 支持 m³ 等特殊符号
- `noto_sans_sc_16/24` — CJK字体，仅含菜单所需汉字，字符列表在 `chars.txt`

**新增中文菜单项时：**
1. 在 `chars.txt` 追加新汉字
2. 用 LVGL 字体工具重新生成 `noto_sans_sc_16.c` 和 `noto_sans_sc_24.c`
3. 修正生成文件的 `fallback` 指向 `my_font_montserrat_16/24`
4. 修正 `noto_sans_sc_16.c` 的 `line_height=20`, `base_line=4`

少量新增字符时，可使用 `merge_glyphs.py` 脚本直接将字形数据合并到 `noto_sans_sc_16.c`（不支持24pt版本），避免重新生成整个字体。需修改脚本中的 `new_glyphs` 数组和 `list_length` 计数。

### 异步UI操作
**从按键回调等非LVGL上下文修改UI时，必须使用 `lv_async_call()`**，否则触发 rendering_in_progress 断言。

## UI 数据更新架构 (MVVM)

```
app_model_update() (1秒定时器) → lv_subject_set_*() → Observer 自动刷新UI控件
```

**数据流路径：** `App/app_model.h` (AppDataModel) → `App/ui/ui_conf.h` (8个string subject) → `App/ui/ui.c` (observer绑定UI控件)

**添加新数据字段的步骤：**
1. `App/app_model.h` 的 `AppDataModel` 添加字段
2. `App/ui/ui_conf.h` 添加对应的 subject
3. `App/ui/ui.c` 的 `ui_create()` 初始化 subject，`ui_update_timer_cb()` 同步数据
4. 用 `lv_subject_add_observer_obj()` 绑定到UI控件

## 多语言系统 (`App/ui/ui_lang.c/h`)

设置菜单支持中英文切换，通过 `lang_id_t` 枚举（~63项）索引双语字符串查找表。`SystemConfig_t.language` 字段控制当前语言（0=英文, 1=中文，默认中文）。

**语言相关的字体切换：** `lang_get_font_14/16/18/20/24()` 根据当前语言自动返回对应字体——中文模式返回 CJK 字体（`noto_sans_sc_*`），英文模式返回 Montserrat 字体。新增翻译项时在 `lang_id_t` 枚举追加ID，在 `ui_lang.c` 的查找表对应位置添加中英文字符串。

## 按键处理架构

```
button_scan_tas (10ms) → button_driver_scan() (消抖/长按状态机)
    → app_button_event_handler() → 按 active_screen 分发到对应handler
```

**按键：** OK(确认) / UP(上) / DOWN(下) / SHIFT(位移)
**事件：** SHORT (<2秒) / LONG (>=2秒, 到达阈值立即触发)

**主屏幕行为：** SHORT UP/DOWN 切换瓦片页，LONG OK 进入设置（需密码时先进入密码屏）

**添加新页面按键处理：** 在 `App/app_button.c` 添加handler → 在 `app_button_event_handler()` 添加分支 → 在 `ui_conf.h` 的 `ui_manager_t` 添加屏幕指针

## Modbus 协议栈 (`Interface/`)

独立于应用层的通用 Modbus 协议实现，不依赖 FreeRTOS 或 HAL：

| 文件 | 功能 |
|------|------|
| `modbus.c/h` | Modbus RTU 协议栈核心 (帧解析、CRC校验、异常响应) |
| `modbus_master.c/h` | Modbus 主机 (传感器轮询，异步非阻塞，回调驱动) |
| `modbus_slave.c/h` | Modbus 从机 (保持寄存器读写，写回调 `modbus_slave_on_write()`) |

**数据流：** `modbus_master_t` 任务 (10ms) → `modbus_master_poll()` → 传感器回调 → `app_sensor` 更新水位 → `modbus_slave_ta` 任务 (10ms) 每1秒将数据同步到保持寄存器 → 上位机通过 UART2 读取。

**添加新寄存器：**
1. 在 `Core/Inc/global.h` 添加地址宏和默认值
2. 在 `App/app_modbus_slave.c` 的 `app_modbus_slave_init_registers()` 初始化初值
3. 如需可写，在 `app_modbus_slave_on_write()` 添加写处理
4. 在 `modbus_slave_ta` 的1秒同步逻辑中更新寄存器值

## 关键设计决策

### 系统配置持久化 (`App/app_config`)
EEPROM (AT24C02) 存储 `SystemConfig_t`，getter/setter 模式访问。修改后需 `app_config_save()` 持久化。Modbus从机写回参数使用脏标记 + **3秒延迟保存**（`CONFIG_SAVE_DELAY_MS`），由 `app_config_process()` 在 log_task 中执行。EEPROM 使用互斥锁，config 和 flow_calc 共享。

**配置变更回调：** `app_config_set_change_callback()` 注册 `config_change_callback_t`（最多4个监听者），UI通过此机制响应配置变更（如清除历史缓存）。

### 累计流量双层持久化 (`App/app_flow_calc`)
- **RTC备份寄存器**：每2秒保存（快速、非阻塞）— DR1/DR2 存 total_flow(double), DR3 存 magic, DR4 存 total_time
- **EEPROM**：每5分钟保存（地址232，24字节结构体 `TotalFlowStorage_t`）— 实际写入延迟到 `flow_calc_process()` 在 log_task 中执行
- **加载优先级**：备份寄存器 → EEPROM → 默认0
- **校验**：magic = `0x5A5A5A5A`，total_flow 范围 [0, 1e12)

### 报警滞回逻辑 (`App/app_alarm`)
4级报警控制4路继电器，由 `flow_calc_update()` 每秒调用。传感器离线时所有报警清除、继电器关闭。

| 类型 | 继电器 | 触发 | 恢复 |
|------|--------|------|------|
| AH (上限) | relay1/PA4 | flow >= AH | flow < AH - DH |
| AL (下限) | relay2/PA5 | flow <= AL | flow > AL + DL |
| AAH (上上限) | relay3/PA6 | flow >= AAH | flow < AAH - DH (与AH共用回差DH) |
| AAL (下下限) | relay4/PA7 | flow <= AAL | flow > AAL + DL (与AL共用回差DL) |

**关键：** AAH/AAL 没有独立的回差值，分别与 AH/AL 共用 DH/DL。

### 4-20mA电流输出 (`App/app_current`)
TIM3 CH4 (PB1) PWM → RC低通 → V/I转换。TIM3配置：PSC=29, ARR=6999 → 84MHz/30/7000 = 400Hz PWM，~7000级分辨率（约12.8 bit）。默认4mA CCR=1006，20mA CCR=3811，有效步数2805级。

线性插值：`ratio = (flow - range_4ma) / (range_20ma - range_4ma)`，映射到 `ccr_4ma ~ ccr_20ma`。`calibration_4ma/20ma` 是工厂校准的CCR值，`range_4ma/20ma` 是用户可调的流量量程端点(m³/h)。20mA量程上限 = `flow_calc_get_max_flow_m3h()`（理论最大流量 × 1.2 裕量）。校准模式有两种路径：UI路径和Modbus路径（10秒超时）。

**已知问题：** `app_current.c` 中 `#define TIM_ARR 7999` 和文件头注释 `ARR=1999` 均与 CubeMX 实际值 6999 不一致，属于历史遗留死代码，不影响功能。

### 水位计算 (`App/app_sensor`)
`水位(mm) = 安装高度 - 距离`。传感器参数（高度、量程等）不在启动时推送，而是在传感器**首次上线**时自动同步。传感器设置均为异步非阻塞，结果通过回调获取。

### 流量公式 (`App/app_flow_calc`)
- 巴歇尔槽: Q = K × H^n（16种标准喉部宽度）
- 三角堰: Q = K × H^2.5（90/60/45/30/22.5度）
- 矩形堰: Q = K × b × H^1.5（0.5/1.0/1.5/2.0m）

**收缩堰修正：** 矩形堰计算内置侧收缩修正——当 `channel_width > 0` 且 `channel_width < weir_width` 时，有效宽度 `b_eff = b - 0.2 × H`。`channel_width` 和 `weir_height` 通过 Modbus 寄存器 0x0109/0x010A 配置。

流量单位由 `instant_unit` 配置决定：L/s, L/min, L/h, m³/h, m³/s, m³/min, T/h, G/h

### Modbus寄存器映射 (`Core/Inc/global.h`)
寄存器地址和类型全部定义在 global.h 中。主要分组：实时数据(0x0001-0x000D)、报警值(0x000E-0x0018)、传感器参数(0x0065-0x006F)、从机参数(0x0101-0x010A)、工厂校准(0x1001-0x1006)、RTC时间(0x0200-0x0206)。写回调在 `App/app_modbus_slave.c` 的 `app_modbus_slave_on_write()` 中处理。

### 异步日志与数据记录
- **日志系统** (`App/app_log.c/h` + `Drivers/File/log_manager.c/h`)：`app_log_send()` 线程安全，可从任意上下文调用。日志按类型分三类（System/User/Alarm），存储路径 `/LOGS/{SYS|USER|ALARM}/YYYY/MM/DD.log`，默认90天保留。所有文件I/O在 `log_task` 中统一执行。
- **数据记录** (`Drivers/File/data_recorder.c/h`)：按 `DATA_RECORD_INTERVAL_MS`（60秒）间隔记录CSV数据。支持时间范围查询、聚合统计（均值/最大/最小/流量增量/报警计数）、CSV/JSON导出，默认365天保留。

### SD卡数据清除 (`App/app_log`)
`app_log_request_clear_sd()` 线程安全地请求清除SD卡所有数据（日志+数据记录），实际删除在 `log_task` 中异步执行。进度通过 `app_log_get_clear_sd_progress()` 查询（-1=空闲, 0~99=进行中, 100=完成）。清除完成后自动调用 `history_invalidate_cache()` 刷新历史查询缓存。UI中通过设置菜单触发。

### 未实现的硬件功能
以下外设已在硬件表和CubeMX中配置，但应用层代码尚未实现：
- **UART4 / 4G模块 (ML307)** — 无应用层驱动代码
- **SPI2 / LoRa模块 (SX1278)** — 无应用层驱动代码

### 硬件参考文档 (`Doc/`)
`Doc/` 目录包含硬件设计文件，调试硬件相关问题时的重要参考：
- **原理图** (`.SchDoc`)：MCU、电源、传感器接口、继电器、4-20mA、RS485 等各模块电路
- **PCB 文件** (`.PcbDoc`, `.PrjPcb`)：PCB 布局和项目文件
- **技术规范**：`超声波明渠污水流量计技术要求.pdf` 等行业标准文档
- **结构图纸**：LCD 铁框尺寸图纸 (`.dwg`)

## UI页面结构

**主屏幕** — 垂直瓦片视图 (Tileview)，3页：详情页 / 趋势图页(双系列: 10s采样+5min采样) / 累计流量记录页

**趋势图细节：** 10秒采样序列30点（5分钟历史）+ 5分钟采样序列30点（150分钟历史）。Y轴按历史最大流量自动缩放。数据在 `ui_update_timer_cb()` 中每秒递增计数器，每10秒/300秒分别推入对应序列。

**设置页面** — 三级菜单：分类列表 → 参数列表 → 数值编辑（SHIFT键切换步进量级）。15秒无操作自动返回主屏幕。进入设置前可选密码验证（固定密码1234）。

**设置项双路径编辑** (`set_item_t` in `ui_set_page.c`)：参数项支持四种数据路径：
- **uint32路径**：`get/set` 回调 + `step/min_val/max_val` — 整数参数（高度、地址等）
- **float路径**：`getf/setf` 回调 + `stepf/min_valf/max_valf/f_decimals` — 浮点参数（量程、报警值等）
- **double路径**：仅累计流量使用，独立步进列表
- **format路径**：`format()` 回调自定义显示（波特率、停止位等），值在 min~max 间循环

添加新设置项时，在对应分类的 `xxx_items[]` 数组中追加 `set_item_t`，选择合适的数据路径即可。

**屏幕切换：** `ui_switch_screen(new_screen, anim_type, time)`，通过 `ui_manager->active_screen` 跟踪当前屏幕。

**历史数据查询页** (`App/ui/ui_history.c`) — 主屏幕长按SHIFT进入，包含两个子页面：
- **日期选择器**：UP/DOWN调整数值，SHIFT切换年/月/日/时字段
- **数据浏览**：显示3条可见记录（上一条/当前/下一条），滑动窗口缓存21条，边缘预加载
- **跨任务查询**：按键回调仅修改状态标记，UI操作通过 `lv_async_call()` 执行，文件I/O由 `history_query_process()` 在 log_task 中完成（使用序号同步 query/commit/cancel）
- 15秒无操作自动返回主屏幕
