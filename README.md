# OCFM_V2 - 明渠流量计

**Open Channel Flow Meter Firmware V2**

基于 STM32F407 的智能明渠流量计固件，通过超声波/雷达水位计测量水位，结合标准堰槽公式（巴歇尔槽、三角堰、矩形堰）计算瞬时流量和累计流量。适用于污水处理、水利灌溉、环保监测等场景。

## 功能特性

- **多堰槽类型支持** - 巴歇尔槽（16种标准喉部宽度）、三角堰（5种标准角度）、矩形堰（4种标准宽度）
- **传感器接口** - 支持雷达/超声波水位传感器，Modbus RTU 通信
- **实时数据采集** - 水位、瞬时流量、累计流量、温度实时监测
- **趋势图显示** - 双采样率折线图（10s / 5min），各30个数据点，Y轴自动缩放
- **历史数据记录** - 按日期分目录 CSV 存储，支持时间范围查询与聚合统计
- **4-20mA 电流输出** - 可配置量程，PWM 驱动 V/I 转换，支持工厂校准
- **Modbus RTU 从机** - 标准保持寄存器映射，支持远程读写配置与数据采集
- **多级报警** - 上限/下限/上上限/下下限四组报警，带回差设置
- **4路继电器输出** - 可由报警或流量阈值驱动
- **数据持久化** - 累计流量双层保护（RTC备份寄存器 + EEPROM），断电不丢失
- **SD卡数据存储** - 历史数据、系统日志、用户日志、报警日志
- **LVGL 图形界面** - 2.8寸彩色 LCD，三级设置菜单，中英文切换，按键操作

## 硬件规格

| 项目 | 规格 |
|------|------|
| MCU | STM32F407VGTx (Cortex-M4, 168MHz, FPU) |
| LCD | 2.8寸 ST7789 (FSMC 8080并口) |
| 传感器接口 | UART1 RS485 (Modbus RTU 主机) |
| 通信接口 | UART2 RS485 (Modbus RTU 从机) + UART4 (4G模块 ML307, 未实现) |
| 无线 | LoRa (SX1278, SPI2, 未实现) |
| 存储 | EEPROM AT24C02 (I2C2) + SD卡 (SDIO) |
| 温度 | CT1820 (1-Wire, PB0) |
| 按键 | 4路 (确认/上/下/位移) |
| 继电器 | 4路 |
| 蜂鸣器/背光 | TIM3 PWM |

## 软件架构

```
OCFM_V2/
├── Core/                    # STM32 HAL & CubeMX 生成代码
│   ├── Inc/global.h         # 全局配置、寄存器定义、SystemConfig_t
│   ├── Inc/rtc_time.h       # 统一 RTC 时间 API
│   └── Src/                 # 外设初始化 (main.c, freertos.c)
├── App/                     # 应用层
│   ├── ui/                  # LVGL UI (Tileview主屏、三级设置菜单、趋势图)
│   ├── app_model.c/h        # 数据模型 (MVVM, Observer模式)
│   ├── app_config.c/h       # 系统配置 (EEPROM, getter/setter)
│   ├── app_flow_calc.c/h    # 流量计算 (堰槽公式, 累计流量)
│   ├── app_sensor.c/h       # 传感器应用层 (Modbus主机封装)
│   ├── app_modbus_slave.c/h # Modbus从机 (寄存器映射, 写回调)
│   ├── app_button.c/h       # 按键处理
│   └── app_log.c/h          # 日志初始化
├── Drivers/
│   ├── AT24C02/             # EEPROM 驱动
│   ├── Button/              # 按键驱动 (消抖/长按)
│   ├── CT1820/              # 温度传感器驱动
│   └── File/                # 文件系统抽象层
│       ├── file_driver.c/h  # FATFS 封装
│       ├── data_recorder.c/h# CSV 历史数据记录
│       └── log_manager.c/h  # 系统日志管理
├── Interface/
│   ├── modbus.c/h           # Modbus 协议栈
│   ├── modbus_master.c/h    # Modbus 主机
│   └── modbus_slave.c/h     # Modbus 从机
├── Middlewares/
│   ├── lvgl/                # LVGL 9.5.0
│   └── Third_Party/         # FreeRTOS, FatFs
└── Doc/                     # 硬件原理图 & 技术文档
```

## 技术栈

| 组件 | 版本 |
|------|------|
| RTOS | FreeRTOS 10.3.1 |
| GUI | LVGL 9.5.0 |
| 文件系统 | FatFs |
| HAL | STM32F4xx HAL |
| 构建工具 | Keil MDK-ARM (AC5) |

## FreeRTOS 任务架构

| 任务 | 栈 | 优先级 | 功能 |
|------|----|--------|------|
| main_task | 8192 | Normal | LVGL 刷新 + 模块初始化 |
| log_task | 8192 | Low | 日志输出、EEPROM保存、数据记录、历史查询、RTC 更新 |
| button_scan_tas | 2048 | Low | 按键扫描 (10ms) |
| modbus_master_t | 1024 | Low | 传感器轮询 (10ms) |
| modbus_slave_ta | 1024 | Low | 从机通信 (10ms, 1s 数据同步) |

## 流量计算公式

- **巴歇尔槽 (Parshall Flume)** — Q = K x H^n（16种标准喉部宽度）
- **三角堰 (Triangular Weir)** — Q = K x H^2.5（90°/60°/45°/30°/22.5°）
- **矩形堰 (Rectangular Weir)** — Q = K x b x H^1.5（0.5/1.0/1.5/2.0m）

支持流量单位：L/s、L/min、L/h、m³/h、m³/s、m³/min、T/h、G/h

## Modbus RTU 寄存器映射 (部分)

| 地址 | 名称 | 类型 | 说明 |
|------|------|------|------|
| 0x0001 | REG_WUWEI | uint16 | 水位 |
| 0x0002 | REG_DISTANCE | uint16 | 距离 |
| 0x0003 | REG_TEMPERATURE | uint16 | 温度 |
| 0x0004-0x0005 | REG_INSTANT_FLOW | float | 瞬时流量 |
| 0x0006-0x0009 | REG_SUM_FLOW | double | 累计流量 |
| 0x000A-0x000D | 继电器状态 | uint16 x4 | 继电器 1-4 |
| 0x000E-0x0018 | 报警值 | float x6 | 上限/下限/上上限/下下限及回差 |
| 0x0065-0x006F | 传感器参数 | uint16 | 量程、高度、滤波等 |
| 0x0101-0x010C | 从机参数 | uint16/float | 渠道类型、流量单位、4-20mA量程、渠宽、堰高、水位上下限 |
| 0x0200-0x0206 | RTC 时间 | uint16 | 年月日时分秒星期 |

## 构建与烧录

1. 打开 Keil 工程文件 `MDK-ARM/OCFM_V2.uvprojx`
2. 编译: `Project -> Build Target` (F7)
3. 下载: `Flash -> Download` (F8)

## UI 交互

- **主屏幕** — 垂直瓦片视图：详情页 / 趋势图页 / 累计流量页，上下键切换
- **设置菜单** — 三级结构：分类列表 -> 参数列表 -> 数值编辑，SHIFT键切换步进量级
- **历史查询** — 长按SHIFT进入，日期选择器 + 滑动窗口浏览
- **自动返回** — 设置页面 15 秒无操作自动返回主屏幕

## 许可证

本项目为私有项目，未经授权不得复制或分发。
