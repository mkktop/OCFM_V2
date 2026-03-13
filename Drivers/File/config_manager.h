/**
 * @file config_manager.h
 * @brief 配置管理器头文件
 * @details 提供INI格式配置文件的读写功能
 *          支持默认值、配置校验、原子写入
 */

#ifndef __CONFIG_MANAGER_H
#define __CONFIG_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "file_driver.h"

/**
 * @brief 配置项最大数量
 */
#define CONFIG_MAX_ITEMS    64

/**
 * @brief 键名最大长度
 */
#define CONFIG_KEY_MAX_LEN  32

/**
 * @brief 值最大长度
 */
#define CONFIG_VALUE_MAX_LEN 128

/**
 * @brief 配置文件路径
 */
#define CONFIG_FILE_PATH    "/config/system.ini"
#define CONFIG_BACKUP_PATH  "/config/system.ini.bak"

/**
 * @brief 配置类型枚举
 */
typedef enum {
    CONFIG_TYPE_INT = 0,    /**< 整数类型 */
    CONFIG_TYPE_FLOAT,      /**< 浮点类型 */
    CONFIG_TYPE_STRING,     /**< 字符串类型 */
    CONFIG_TYPE_BOOL        /**< 布尔类型 */
} ConfigType;

/**
 * @brief 配置项结构体
 */
typedef struct {
    char key[CONFIG_KEY_MAX_LEN];       /**< 配置键名 */
    char value[CONFIG_VALUE_MAX_LEN];   /**< 配置值 */
    ConfigType type;                     /**< 值类型 */
    uint8_t modified;                    /**< 是否被修改 */
} ConfigItem;

/**
 * @brief 配置管理器结构体
 */
typedef struct {
    ConfigItem items[CONFIG_MAX_ITEMS]; /**< 配置项数组 */
    uint32_t count;                      /**< 当前配置项数量 */
    uint8_t loaded;                      /**< 是否已加载 */
    uint32_t version;                    /**< 配置版本号 */
} ConfigManager;

/**
 * @brief 系统配置参数枚举
 * @note 根据明渠流量计实际需求定义
 */
typedef enum {
    /* 系统参数 */
    CFG_SYS_DEVICE_ID = 0,      /**< 设备ID */
    CFG_SYS_VERSION,            /**< 固件版本 */
    CFG_SYS_DATE_FORMAT,        /**< 日期格式 */

    /* 传感器参数 */
    CFG_SENSOR_TYPE,            /**< 传感器类型(0=超声波 1=雷达) */
    CFG_SENSOR_OFFSET,          /**< 传感器安装高度偏移(m) */
    CFG_SENSOR_RANGE_MAX,       /**< 最大量程(m) */
    CFG_SENSOR_RANGE_MIN,       /**< 最小量程(m) */

    /* 流量计算参数 */
    CFG_FLOW_WEIR_TYPE,         /**< 堰槽类型(0=巴歇尔槽 1=三角堰 2=矩形堰) */
    CFG_FLOW_WEIR_SIZE,         /**< 堰槽尺寸型号 */
    CFG_FLOW_C_FACTOR,          /**< 流量系数C */
    CFG_FLOW_N_EXPONENT,        /**< 指数n */
    CFG_FLOW_UNIT,              /**< 流量单位(0=m³/s 1=m³/h 2=L/s) */

    /* 报警参数 */
    CFG_ALARM_HIGH_LEVEL,       /**< 高水位报警值(m) */
    CFG_ALARM_LOW_LEVEL,        /**< 低水位报警值(m) */
    CFG_ALARM_HIGH_FLOW,        /**< 高流量报警值 */
    CFG_ALARM_ENABLE,           /**< 报警使能位图 */

    /* 通信参数 */
    CFG_COMM_BAUDRATE,          /**< 波特率 */
    CFG_COMM_SLAVE_ADDR,        /**< Modbus从机地址 */
    CFG_COMM_PARITY,            /**< 校验位 */

    /* 存储参数 */
    CFG_STORAGE_LOG_INTERVAL,   /**< 日志记录间隔(s) */
    CFG_STORAGE_DATA_INTERVAL,  /**< 数据存储间隔(s) */
    CFG_STORAGE_RETENTION_DAYS, /**< 数据保留天数 */

    CFG_ITEM_COUNT              /**< 配置项总数 */
} ConfigParamId;

/**
 * @brief 配置参数定义结构体
 */
typedef struct {
    ConfigParamId id;           /**< 参数ID */
    const char* key;            /**< 参数键名 */
    const char* default_value;  /**< 默认值 */
    ConfigType type;            /**< 参数类型 */
    const char* description;    /**< 参数描述 */
} ConfigParamDef;

/**
 * @brief 配置管理器初始化
 * @return 0:成功 1:失败
 * @note 加载配置文件，不存在则使用默认值
 */
uint8_t config_manager_init(void);

/**
 * @brief 从文件加载配置
 * @return 0:成功 1:失败
 */
uint8_t config_load(void);

/**
 * @brief 保存配置到文件
 * @return 0:成功 1:失败
 * @note 使用原子写入，确保配置不损坏
 */
uint8_t config_save(void);

/**
 * @brief 重置为默认配置
 * @return 0:成功 1:失败
 */
uint8_t config_reset_to_default(void);

/**
 * @brief 获取字符串配置值
 * @param id 配置项ID
 * @param value 输出缓冲区
 * @param size 缓冲区大小
 * @return 0:成功 1:失败
 */
uint8_t config_get_string(ConfigParamId id, char* value, uint32_t size);

/**
 * @brief 获取整数配置值
 * @param id 配置项ID
 * @param value 输出值指针
 * @return 0:成功 1:失败
 */
uint8_t config_get_int(ConfigParamId id, int32_t* value);

/**
 * @brief 获取浮点配置值
 * @param id 配置项ID
 * @param value 输出值指针
 * @return 0:成功 1:失败
 */
uint8_t config_get_float(ConfigParamId id, float* value);

/**
 * @brief 获取布尔配置值
 * @param id 配置项ID
 * @param value 输出值指针
 * @return 0:成功 1:失败
 */
uint8_t config_get_bool(ConfigParamId id, uint8_t* value);

/**
 * @brief 设置字符串配置值
 * @param id 配置项ID
 * @param value 值字符串
 * @return 0:成功 1:失败
 */
uint8_t config_set_string(ConfigParamId id, const char* value);

/**
 * @brief 设置整数配置值
 * @param id 配置项ID
 * @param value 整数值
 * @return 0:成功 1:失败
 */
uint8_t config_set_int(ConfigParamId id, int32_t value);

/**
 * @brief 设置浮点配置值
 * @param id 配置项ID
 * @param value 浮点值
 * @param precision 小数精度
 * @return 0:成功 1:失败
 */
uint8_t config_set_float(ConfigParamId id, float value, uint8_t precision);

/**
 * @brief 设置布尔配置值
 * @param id 配置项ID
 * @param value 布尔值(0或1)
 * @return 0:成功 1:失败
 */
uint8_t config_set_bool(ConfigParamId id, uint8_t value);

/**
 * @brief 通过键名获取字符串值
 * @param key 键名
 * @param value 输出缓冲区
 * @param size 缓冲区大小
 * @return 0:成功 1:失败
 */
uint8_t config_get_string_by_key(const char* key, char* value, uint32_t size);

/**
 * @brief 通过键名设置字符串值
 * @param key 键名
 * @param value 值字符串
 * @return 0:成功 1:失败
 */
uint8_t config_set_string_by_key(const char* key, const char* value);

/**
 * @brief 导出配置为字符串
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 实际写入长度
 */
uint32_t config_export(char* buffer, uint32_t size);

/**
 * @brief 从字符串导入配置
 * @param data 配置字符串
 * @return 0:成功 1:失败
 */
uint8_t config_import(const char* data);

/**
 * @brief 打印当前所有配置
 */
void config_print_all(void);

/**
 * @brief 获取配置版本号
 * @return 版本号
 */
uint32_t config_get_version(void);

/**
 * @brief 检查配置是否修改
 * @return 1:已修改 0:未修改
 */
uint8_t config_is_modified(void);

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_MANAGER_H */
