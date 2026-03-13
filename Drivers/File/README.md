# 文件系统驱动模块使用说明

本模块为明渠流量计（OCFM_V2）提供完整的文件存储管理功能，包含三个子模块：

- **日志管理器 (log_manager)** - 系统日志、操作日志、报警日志
- **配置管理器 (config_manager)** - INI格式参数配置
- **数据记录器 (data_recorder)** - CSV格式流量历史数据

## 目录结构

```
Drivers/File/
├── file_driver.c/h     # 基础文件操作（基于FatFs）
├── log_manager.c/h     # 日志管理器
├── config_manager.c/h  # 配置管理器
├── data_recorder.c/h   # 数据记录器
└── README.md           # 本文件
```

---

## 一、日志管理器 (log_manager)

### 功能特性

- 支持4种日志类型：系统日志、操作日志、流量数据、报警日志
- 支持4种日志级别：DEBUG、INFO、WARN、ERROR
- 按日期自动分文件存储
- 文件循环覆盖机制
- 支持串口控制台和SD卡双输出

### SD卡文件结构

```
/logs/
├── system/             # 系统日志
│   ├── system_20240313_000.txt
│   └── system_20240313_001.txt
├── oper/               # 操作日志
│   └── oper_20240313_000.txt
├── flow/               # 流量数据日志
│   └── flow_20240313_000.txt
└── alarm/              # 报警日志
    └── alarm_20240313_000.txt
```

### API使用示例

```c
#include "log_manager.h"

void log_example(void)
{
    /* 1. 初始化日志管理器 */
    LogManagerConfig config = {
        .enable_console = 1,        // 输出到串口
        .enable_sd = 1,             // 保存到SD卡
        .min_level = LOG_LEVEL_INFO, // 只记录INFO及以上级别
        .max_file_size = 1024*1024,  // 单文件最大1MB
        .max_file_count = 10,        // 10个文件循环覆盖
        .base_path = "/logs"
    };
    log_manager_init(&config);

    /* 2. 写入系统日志 */
    log_system("SYSTEM_START", "Device powered on");
    log_write(LOG_LEVEL_INFO, "Sensor initialized, type: %d", 0);

    /* 3. 记录操作日志 */
    log_operation("Admin", "CALIBRATE", "Success");

    /* 4. 记录报警 */
    log_alarm("HIGH_LEVEL", 2, "Water level exceeds threshold: 4.5m");

    /* 5. 记录流量数据 */
    log_flow_data("2024-03-13 12:00:00", 2.345f, 12.5f, 12345.67);

    /* 6. 查看日志文件 */
    log_list_files(LOG_TYPE_SYSTEM);
}
```

---

## 二、配置管理器 (config_manager)

### 功能特性

- INI格式配置文件
- 预定义参数结构，包含流量计所有配置项
- 支持默认值、配置校验
- 原子写入（防配置损坏）
- 支持导入导出

### 预定义配置参数

| 参数ID | 键名 | 类型 | 默认值 | 说明 |
|--------|------|------|--------|------|
| CFG_SYS_DEVICE_ID | device_id | 字符串 | OCFM0001 | 设备ID |
| CFG_SENSOR_TYPE | sensor_type | 整数 | 0 | 传感器类型 |
| CFG_SENSOR_OFFSET | sensor_offset | 浮点 | 0.0 | 传感器安装偏移 |
| CFG_FLOW_WEIR_TYPE | weir_type | 整数 | 0 | 堰槽类型 |
| CFG_FLOW_C_FACTOR | c_factor | 浮点 | 1.0 | 流量系数 |
| CFG_ALARM_HIGH_LEVEL | alarm_high_level | 浮点 | 4.5 | 高水位报警值 |
| CFG_COMM_SLAVE_ADDR | slave_addr | 整数 | 1 | Modbus从机地址 |
| ... | ... | ... | ... | ... |

### 配置文件示例 (system.ini)

```ini
# OCFM_V2 System Configuration
# Version: 1

[system]
device_id=OCFM0001
version=2.0.0
date_format=YYYY-MM-DD

[sensor]
sensor_type=0
sensor_offset=0.000
sensor_range_max=5.000
sensor_range_min=0.000

[flow]
weir_type=0
weir_size=1
c_factor=1.000
n_exponent=1.500
flow_unit=0

[alarm]
alarm_high_level=4.500
alarm_low_level=0.100
alarm_high_flow=100.000
alarm_enable=1

[communication]
baudrate=9600
slave_addr=1
parity=0

[storage]
log_interval=60
data_interval=300
retention_days=30
```

### API使用示例

```c
#include "config_manager.h"

void config_example(void)
{
    /* 1. 初始化（自动加载配置文件，不存在则使用默认值） */
    config_manager_init();

    /* 2. 读取配置 */
    char device_id[32];
    config_get_string(CFG_SYS_DEVICE_ID, device_id, sizeof(device_id));

    float offset;
    config_get_float(CFG_SENSOR_OFFSET, &offset);

    int32_t slave_addr;
    config_get_int(CFG_COMM_SLAVE_ADDR, &slave_addr);

    /* 3. 修改配置 */
    config_set_float(CFG_SENSOR_OFFSET, 0.5f, 3);  // 3位小数精度
    config_set_int(CFG_COMM_SLAVE_ADDR, 5);

    /* 4. 保存配置到文件 */
    config_save();

    /* 5. 打印所有配置 */
    config_print_all();

    /* 6. 重置为默认值 */
    // config_reset_to_default();
    // config_save();
}
```

---

## 三、数据记录器 (data_recorder)

### 功能特性

- CSV格式存储历史数据
- 支持时间范围查询
- 数据统计功能
- 自动清理过期数据
- 数据导出功能

### 数据记录格式

```csv
timestamp,water_level,instant_flow,total_flow,temperature,flags
2024-03-13 12:00:00,2.345,12.500000,12345.670000,25.5,0
2024-03-13 12:05:00,2.356,12.650000,12349.450000,25.6,0
...
```

### SD卡文件结构

```
/data/
└── history.csv         # 历史数据文件
```

### API使用示例

```c
#include "data_recorder.h"

void recorder_example(void)
{
    /* 1. 初始化 */
    DataRecorderConfig config = {
        .enable = 1,
        .interval_sec = 300,    // 5分钟记录一次
        .retention_days = 30,   // 保留30天
        .csv_header = 1,
        .max_file_size = 10*1024*1024  // 10MB
    };
    data_recorder_init(&config);

    /* 2. 记录流量数据（简化接口） */
    data_record_flow(
        2.345f,         // 水位(m)
        12.5f,          // 瞬时流量(m³/s)
        12345.67,       // 累计流量(m³)
        25.5f,          // 温度(°C)
        0               // 标志位
    );

    /* 3. 记录流量数据（完整接口） */
    DataRecord record = {
        .year = 2024, .month = 3, .day = 13,
        .hour = 12, .minute = 0, .second = 0,
        .water_level = 2.345f,
        .instant_flow = 12.5f,
        .total_flow = 12345.67,
        .temperature = 25.5f,
        .flags = 0x0001  // 报警标志
    };
    data_record(&record);

    /* 4. 查询数据 */
    DataQueryFilter filter = {
        .start_year = 2024, .start_month = 3, .start_day = 13,
        .start_hour = 0, .start_min = 0,
        .end_year = 2024, .end_month = 3, .end_day = 13,
        .end_hour = 23, .end_min = 59,
        .filter_alarm_only = 0,
        .flags_mask = 0
    };

    /* 使用回调处理每条记录 */
    uint32_t count = data_query(&filter, data_callback, NULL);
    printf("Query returned %lu records\r\n", count);

    /* 5. 按日期查询 */
    count = data_query_by_date(2024, 3, 13, data_callback, NULL);

    /* 6. 清理过期数据（保留30天） */
    data_cleanup(30);

    /* 7. 格式化（清空所有数据） */
    // data_format();
}

/* 数据查询回调函数 */
void data_callback(const DataRecord* record, void* user_data)
{
    (void)user_data;
    printf("Time: %04u-%02u-%02u %02u:%02u:%02u, ",
           record->year, record->month, record->day,
           record->hour, record->minute, record->second);
    printf("Level: %.3f m, Flow: %.3f m³/s\r\n",
           record->water_level, record->instant_flow);
}
```

---

## 四、集成到FreeRTOS任务

### 任务规划示例

```c
/* freertos.c 中定义任务 */

void TaskStorage(void *argument)
{
    /* 初始化文件系统模块 */
    log_manager_init(NULL);
    config_manager_init();
    data_recorder_init(NULL);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);

    /* 记录间隔计数器 */
    uint32_t data_counter = 0;
    uint32_t log_counter = 0;

    for (;;)
    {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        /* 每5分钟记录一次流量数据 */
        if (++data_counter >= 300) {
            data_counter = 0;

            float level = get_water_level();
            float instant = calculate_flow(level);
            double total = get_total_flow();

            data_record_flow(level, instant, total, 25.0f, 0);
        }

        /* 每分钟检查并记录系统状态 */
        if (++log_counter >= 60) {
            log_counter = 0;

            /* 记录内存使用情况（示例） */
            // log_write(LOG_LEVEL_DEBUG, "Free heap: %u bytes", xPortGetFreeHeapSize());
        }

        /* 每天凌晨清理过期数据 */
        // if (is_midnight()) {
        //     data_cleanup(30);
        //     log_cleanup(30);
        // }
    }
}
```

---

## 五、内存占用估算

| 模块 | ROM (Flash) | RAM (静态) |
|------|-------------|------------|
| file_driver | ~2KB | ~64B |
| log_manager | ~4KB | ~256B |
| config_manager | ~6KB | ~8KB (配置缓存) |
| data_recorder | ~4KB | ~256B |
| **总计** | ~16KB | ~9KB |

---

## 六、注意事项

1. **RTC时间**: 实际项目中需要实现RTC时间获取，替换代码中的示例时间
2. **互斥保护**: 多任务环境需添加文件操作互斥锁
3. **SD卡检测**: 建议定期检测SD卡状态，支持热插拔
4. **错误处理**: 生产环境需完善错误处理和重试机制
5. **文件重命名**: FatFs 的 `f_rename()` 用于实现原子写入，当前为简化实现
