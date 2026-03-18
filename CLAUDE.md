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
│   └── app_log.c/h          # 日志功能
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

**注意：** 当前实现使用简化任务模型。完整架构规划见 `.trae/documents/明渠流量计架构设计.md`

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

## Modbus寄存器映射

主要保持寄存器 (完整列表见 Core/Inc/global.h)：
- 0x0001: 水位 (uint16)
- 0x0002: 距离 (uint16)
- 0x0003: 温度 (uint16)
- 0x0004-0x0005: 瞬时流量 (float, 占2个寄存器)
- 0x0006-0x0009: 累计流量 (double, 占4个寄存器)
- 0x000A-0x000D: 继电器状态 (各占1个uint16)

## 重要文件

- `Core/Inc/global.h` - 系统配置、寄存器定义、数据结构
- `Core/Src/freertos.c` - FreeRTOS任务定义
- `Middlewares/lvgl/lv_conf.h` - LVGL配置
- `.eide/eide.yml` - EIDE构建配置
- `.trae/documents/明渠流量计架构设计.md` - 详细架构设计文档

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

详见 `UI_DATA_ARCHITECTURE.md`