# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

OCFM_V2 是一个明渠流量计固件项目，基于 STM32F407VGTx 单片机。通过超声波/雷达水位计测量水位，结合堰槽公式（巴歇尔槽、三角堰、矩形堰）计算瞬时流量和累计流量。

**核心技术栈：** STM32F407VGTx (Cortex-M4, 168MHz) | FreeRTOS V10.3.1 | LVGL 9.5.0 | FatFs | Keil MDK-ARM (AC5)

**关键约束：**
- EEPROM AT24C02 仅 256 字节，`SystemConfig_t` 约 160 字节（定义在 `Core/Inc/global.h`）。添加新配置字段前务必确认剩余空间。
- 无自动化测试和lint工具，验证依赖实机调试和串口日志输出。

### 构建
```
打开工程: MDK-ARM/OCFM_V2.uvprojx
编译: Project -> Build Target (F7)
下载: Flash -> Download (F8)
```

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
| log_task | 4096 | 500ms | 日志输出、config_process、flow_process、RTC更新 |
| button_scan_tas | 2048 | 10ms | 按键扫描 |
| modbus_master_t | 1024 | 10ms | 传感器轮询 |
| modbus_slave_ta | 1024 | 10ms | 从机通信 (每1秒同步数据到寄存器) |

| 定时器 | 周期 | 回调 |
|--------|------|------|
| flow_refresh_timer | 1000ms | 读取水位 → flow_calc_update() → app_alarm_update() → app_current_update() |

**系统启动顺序** (main_task_func, `Core/Src/freertos.c`)：
1. `app_config_init()` — 从EEPROM加载配置
2. `app_alarm_init()` — 继电器默认关闭
3. `flow_calc_load_total()` — 从备份寄存器/EEPROM加载累计流量
4. `app_sensor_init()` — 初始化传感器（参数在传感器首次上线时同步，非启动时）
5. `app_current_init()` — 启动TIM3 CH4 PWM，初始输出4mA
6. `lv_init()` + `lv_port_disp_init()` + `ui_create()` — 初始化LVGL和UI
7. 预渲染10帧后开背光，进入 `lv_timer_handler()` 主循环

## 代码规范

- 缩进: 4个空格 | 大括号: Linux风格 | 命名: snake_case函数 / CamelCase类型 / UPPER_SNAKE_CASE宏常量
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

## 按键处理架构

```
button_scan_tas (10ms) → button_driver_scan() (消抖/长按状态机)
    → app_button_event_handler() → 按 active_screen 分发到对应handler
```

**按键：** OK(确认) / UP(上) / DOWN(下) / SHIFT(位移)
**事件：** SHORT (<2秒) / LONG (>=2秒, 到达阈值立即触发)

**主屏幕行为：** SHORT UP/DOWN 切换瓦片页，LONG OK 进入设置（需密码时先进入密码屏）

**添加新页面按键处理：** 在 `App/app_button.c` 添加handler → 在 `app_button_event_handler()` 添加分支 → 在 `ui_conf.h` 的 `ui_manager_t` 添加屏幕指针

## 关键设计决策

### 系统配置持久化 (`App/app_config`)
EEPROM (AT24C02) 存储 `SystemConfig_t`，getter/setter 模式访问。修改后需 `app_config_save()` 持久化。Modbus从机写回参数使用脏标记 + **3秒延迟保存**（`CONFIG_SAVE_DELAY_MS`），由 `app_config_process()` 在 log_task 中执行。EEPROM 使用互斥锁，config 和 flow_calc 共享。

### 累计流量双层持久化 (`App/app_flow_calc`)
- **RTC备份寄存器**：每10秒保存（快速、非阻塞）— DR1/DR2 存 total_flow(double), DR3 存 magic, DR4 存 total_time
- **EEPROM**：每5分钟保存（地址240，20字节结构体）— 实际写入延迟到 `flow_calc_process()` 在 log_task 中执行
- **加载优先级**：备份寄存器 → EEPROM → 默认0
- **校验**：magic = `0x5A5A5A5A`，total_flow 范围 [0, 1e12)

### 报警滞回逻辑 (`App/app_alarm`)
4级报警控制4路继电器，由 `flow_calc_update()` 每秒调用。传感器离线时所有报警清除、继电器关闭。

| 类型 | 继电器 | 触发 | 恢复 |
|------|--------|------|------|
| AH (上限) | relay1/PA4 | flow >= AH | flow < AH - DH |
| AL (下限) | relay2/PA5 | flow <= AL | flow > AL + DL |
| AAH (上上限) | relay3/PA6 | flow >= AAH | flow < AH (恢复到AH，非AAH) |
| AAL (下下限) | relay4/PA7 | flow <= AAL | flow > AL (恢复到AL，非AAL) |

**关键：** AAH/AAL 没有独立的回差值，恢复条件分别参考 AH/AL 阈值。

### 4-20mA电流输出 (`App/app_current`)
TIM3 CH4 (PB1) PWM → RC低通 → V/I转换。线性插值：`ratio = (flow - range_4ma) / (range_20ma - range_4ma)`，映射到 `ccr_4ma ~ ccr_20ma`。`calibration_4ma/20ma` 是工厂校准的CCR值，`range_4ma/20ma` 是用户可调的流量量程端点(m³/h)。校准模式有两种路径：UI路径和Modbus路径（10秒超时）。

### 水位计算 (`App/app_sensor`)
`水位(mm) = 安装高度 - 距离`。传感器参数（高度、量程等）不在启动时推送，而是在传感器**首次上线**时自动同步。传感器设置均为异步非阻塞，结果通过回调获取。

### 流量公式 (`App/app_flow_calc`)
- 巴歇尔槽: Q = K × H^n（16种标准喉部宽度）
- 三角堰: Q = K × H^2.5（90/60/45/30/22.5度）
- 矩形堰: Q = K × b × H^1.5（0.5/1.0/1.5/2.0m）

流量单位由 `instant_unit` 配置决定：L/s, L/min, L/h, m³/h, m³/s, m³/min, T/h, G/h

### Modbus寄存器映射 (`Core/Inc/global.h`)
寄存器地址和类型全部定义在 global.h 中。主要分组：实时数据(0x0001-0x000D)、报警值(0x000E-0x0018)、传感器参数(0x0065-0x006F)、从机参数(0x0101-0x0107)、工厂校准(0x1001-0x1006)、RTC时间(0x0200-0x0206)。写回调在 `App/app_modbus_slave.c` 的 `app_modbus_slave_on_write()` 中处理。

## UI页面结构

**主屏幕** — 垂直瓦片视图 (Tileview)，3页：详情页 / 趋势图页(双系列: 10s采样+5min采样) / 累计流量记录页

**设置页面** — 三级菜单：分类列表 → 参数列表 → 数值编辑（SHIFT键切换步进 1/10/100/1000/10000）。15秒无操作自动返回主屏幕。进入设置前可选密码验证（固定密码1234）。

**屏幕切换：** `ui_switch_screen(new_screen, anim_type, time)`，通过 `ui_manager->active_screen` 跟踪当前屏幕。
