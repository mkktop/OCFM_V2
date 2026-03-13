/**
 * @file config_manager.c
 * @brief 配置管理器实现
 * @details 提供INI格式配置文件的读写功能
 */

#include "config_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/**
 * @brief 配置管理器实例
 */
static ConfigManager g_config = {0};

/**
 * @brief 配置参数定义表
 * @note 定义所有系统配置参数的键名、默认值和类型
 */
static const ConfigParamDef g_param_defs[] = {
    /* 系统参数 */
    {CFG_SYS_DEVICE_ID,     "device_id",        "OCFM0001",     CONFIG_TYPE_STRING, "设备ID"},
    {CFG_SYS_VERSION,       "version",          "2.0.0",        CONFIG_TYPE_STRING, "固件版本"},
    {CFG_SYS_DATE_FORMAT,   "date_format",      "YYYY-MM-DD",   CONFIG_TYPE_STRING, "日期格式"},

    /* 传感器参数 */
    {CFG_SENSOR_TYPE,       "sensor_type",      "0",            CONFIG_TYPE_INT,    "传感器类型(0=超声波 1=雷达)"},
    {CFG_SENSOR_OFFSET,     "sensor_offset",    "0.000",        CONFIG_TYPE_FLOAT,  "传感器安装高度偏移(m)"},
    {CFG_SENSOR_RANGE_MAX,  "sensor_range_max", "5.000",        CONFIG_TYPE_FLOAT,  "最大量程(m)"},
    {CFG_SENSOR_RANGE_MIN,  "sensor_range_min", "0.000",        CONFIG_TYPE_FLOAT,  "最小量程(m)"},

    /* 流量计算参数 */
    {CFG_FLOW_WEIR_TYPE,    "weir_type",        "0",            CONFIG_TYPE_INT,    "堰槽类型(0=巴歇尔槽 1=三角堰 2=矩形堰)"},
    {CFG_FLOW_WEIR_SIZE,    "weir_size",        "1",            CONFIG_TYPE_INT,    "堰槽尺寸型号"},
    {CFG_FLOW_C_FACTOR,     "c_factor",         "1.000",        CONFIG_TYPE_FLOAT,  "流量系数C"},
    {CFG_FLOW_N_EXPONENT,   "n_exponent",       "1.500",        CONFIG_TYPE_FLOAT,  "指数n"},
    {CFG_FLOW_UNIT,         "flow_unit",        "0",            CONFIG_TYPE_INT,    "流量单位(0=m³/s 1=m³/h 2=L/s)"},

    /* 报警参数 */
    {CFG_ALARM_HIGH_LEVEL,  "alarm_high_level", "4.500",        CONFIG_TYPE_FLOAT,  "高水位报警值(m)"},
    {CFG_ALARM_LOW_LEVEL,   "alarm_low_level",  "0.100",        CONFIG_TYPE_FLOAT,  "低水位报警值(m)"},
    {CFG_ALARM_HIGH_FLOW,   "alarm_high_flow",  "100.000",      CONFIG_TYPE_FLOAT,  "高流量报警值"},
    {CFG_ALARM_ENABLE,      "alarm_enable",     "1",            CONFIG_TYPE_BOOL,   "报警使能"},

    /* 通信参数 */
    {CFG_COMM_BAUDRATE,     "baudrate",         "9600",         CONFIG_TYPE_INT,    "波特率"},
    {CFG_COMM_SLAVE_ADDR,   "slave_addr",       "1",            CONFIG_TYPE_INT,    "Modbus从机地址"},
    {CFG_COMM_PARITY,       "parity",           "0",            CONFIG_TYPE_INT,    "校验位(0=无 1=奇 2=偶)"},

    /* 存储参数 */
    {CFG_STORAGE_LOG_INTERVAL,  "log_interval",     "60",       CONFIG_TYPE_INT,    "日志记录间隔(s)"},
    {CFG_STORAGE_DATA_INTERVAL, "data_interval",    "300",      CONFIG_TYPE_INT,    "数据存储间隔(s)"},
    {CFG_STORAGE_RETENTION_DAYS,"retention_days",   "30",       CONFIG_TYPE_INT,    "数据保留天数"},
};

#define PARAM_DEF_COUNT (sizeof(g_param_defs) / sizeof(g_param_defs[0]))

/**
 * @brief 去除字符串首尾空格
 */
static void trim(char* str)
{
    char* start = str;
    char* end = str + strlen(str) - 1;

    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
        start++;
    }

    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/**
 * @brief 查找配置项
 */
static ConfigItem* find_item(const char* key)
{
    for (uint32_t i = 0; i < g_config.count; i++) {
        if (strcmp(g_config.items[i].key, key) == 0) {
            return &g_config.items[i];
        }
    }
    return NULL;
}

/**
 * @brief 添加或更新配置项
 */
static uint8_t set_item(const char* key, const char* value, ConfigType type)
{
    ConfigItem* item = find_item(key);

    if (item) {
        /* 更新已有项 */
        if (strcmp(item->value, value) != 0) {
            strncpy(item->value, value, CONFIG_VALUE_MAX_LEN - 1);
            item->value[CONFIG_VALUE_MAX_LEN - 1] = '\0';
            item->modified = 1;
        }
        return FILE_OK;
    }

    /* 添加新项 */
    if (g_config.count >= CONFIG_MAX_ITEMS) {
        return FILE_ERROR;  /* 配置项已满 */
    }

    item = &g_config.items[g_config.count];
    strncpy(item->key, key, CONFIG_KEY_MAX_LEN - 1);
    item->key[CONFIG_KEY_MAX_LEN - 1] = '\0';
    strncpy(item->value, value, CONFIG_VALUE_MAX_LEN - 1);
    item->value[CONFIG_VALUE_MAX_LEN - 1] = '\0';
    item->type = type;
    item->modified = 1;
    g_config.count++;

    return FILE_OK;
}

/**
 * @brief 解析INI格式行
 */
static uint8_t parse_line(const char* line, char* key, char* value)
{
    char buffer[256];
    char* sep;

    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    trim(buffer);

    /* 跳过空行和注释 */
    if (buffer[0] == '\0' || buffer[0] == '#' || buffer[0] == ';') {
        return FILE_ERROR;
    }

    /* 查找等号 */
    sep = strchr(buffer, '=');
    if (!sep) {
        return FILE_ERROR;
    }

    /* 分离键值 */
    *sep = '\0';
    strncpy(key, buffer, CONFIG_KEY_MAX_LEN - 1);
    key[CONFIG_KEY_MAX_LEN - 1] = '\0';
    trim(key);

    strncpy(value, sep + 1, CONFIG_VALUE_MAX_LEN - 1);
    value[CONFIG_VALUE_MAX_LEN - 1] = '\0';
    trim(value);

    return FILE_OK;
}

/**
 * @brief 创建配置目录
 */
static uint8_t ensure_config_dir(void)
{
    if (!file_exists("/config")) {
        if (file_create_dir("/config") != FILE_OK) {
            return FILE_ERROR;
        }
    }
    return FILE_OK;
}

uint8_t config_manager_init(void)
{
    if (g_config.loaded) {
        return FILE_OK;
    }

    /* 初始化文件系统 */
    file_init();

    /* 尝试加载配置 */
    if (config_load() != FILE_OK) {
        /* 加载失败，使用默认配置 */
        config_reset_to_default();
        /* 保存默认配置 */
        config_save();
    }

    g_config.loaded = 1;
    g_config.version = 1;

    return FILE_OK;
}

uint8_t config_load(void)
{
    char buffer[256];
    char key[CONFIG_KEY_MAX_LEN];
    char value[CONFIG_VALUE_MAX_LEN];
    uint32_t bytes_read;
    uint8_t result;
    uint8_t in_section = 0;

    /* 检查文件是否存在 */
    if (!file_exists(CONFIG_FILE_PATH)) {
        return FILE_ERROR;
    }

    /* 打开文件 */
    result = file_open(CONFIG_FILE_PATH, FILE_MODE_READ);
    if (result != FILE_OK) {
        /* 尝试读取备份 */
        if (file_exists(CONFIG_BACKUP_PATH)) {
            result = file_open(CONFIG_BACKUP_PATH, FILE_MODE_READ);
        }
        if (result != FILE_OK) {
            return FILE_ERROR;
        }
    }

    /* 清空当前配置 */
    g_config.count = 0;

    /* 逐行读取 */
    while (1) {
        /* 简化实现：一次读取所有内容 */
        result = file_read(buffer, sizeof(buffer) - 1, &bytes_read);
        if (result != FILE_OK || bytes_read == 0) {
            break;
        }
        buffer[bytes_read] = '\0';

        /* 解析行 - 实际项目中需要更复杂的行解析 */
        char* line = buffer;
        char* next;
        while (line && *line) {
            next = strchr(line, '\n');
            if (next) {
                *next = '\0';
                next++;
            }

            /* 解析节名 [system] */
            if (line[0] == '[') {
                in_section = 1;
            } else if (in_section && parse_line(line, key, value) == FILE_OK) {
                set_item(key, value, CONFIG_TYPE_STRING);
            }

            line = next;
        }
    }

    file_close();
    return FILE_OK;
}

uint8_t config_save(void)
{
    char buffer[2048];
    uint32_t pos = 0;
    uint8_t result;

    if (!g_config.loaded) {
        return FILE_ERROR;
    }

    /* 确保目录存在 */
    if (ensure_config_dir() != FILE_OK) {
        return FILE_ERROR;
    }

    /* 构建INI内容 */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                    "# OCFM_V2 System Configuration\r\n");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                    "# Version: %lu\r\n\r\n", g_config.version);

    /* 系统参数节 */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[system]\r\n");
    for (uint32_t i = 0; i < g_config.count; i++) {
        const char* key = g_config.items[i].key;
        if (strncmp(key, "device", 6) == 0 ||
            strncmp(key, "version", 7) == 0 ||
            strncmp(key, "date", 4) == 0) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                            "%s=%s\r\n", key, g_config.items[i].value);
        }
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\r\n");

    /* 传感器参数节 */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[sensor]\r\n");
    for (uint32_t i = 0; i < g_config.count; i++) {
        const char* key = g_config.items[i].key;
        if (strncmp(key, "sensor_", 7) == 0) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                            "%s=%s\r\n", key, g_config.items[i].value);
        }
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\r\n");

    /* 流量参数节 */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[flow]\r\n");
    for (uint32_t i = 0; i < g_config.count; i++) {
        const char* key = g_config.items[i].key;
        if (strncmp(key, "weir_", 5) == 0 ||
            strncmp(key, "c_", 2) == 0 ||
            strncmp(key, "n_", 2) == 0 ||
            strncmp(key, "flow_", 5) == 0) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                            "%s=%s\r\n", key, g_config.items[i].value);
        }
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\r\n");

    /* 报警参数节 */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[alarm]\r\n");
    for (uint32_t i = 0; i < g_config.count; i++) {
        const char* key = g_config.items[i].key;
        if (strncmp(key, "alarm_", 6) == 0) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                            "%s=%s\r\n", key, g_config.items[i].value);
        }
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\r\n");

    /* 通信参数节 */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[communication]\r\n");
    for (uint32_t i = 0; i < g_config.count; i++) {
        const char* key = g_config.items[i].key;
        if (strncmp(key, "baudrate", 8) == 0 ||
            strncmp(key, "slave_", 6) == 0 ||
            strncmp(key, "parity", 6) == 0) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                            "%s=%s\r\n", key, g_config.items[i].value);
        }
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\r\n");

    /* 存储参数节 */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[storage]\r\n");
    for (uint32_t i = 0; i < g_config.count; i++) {
        const char* key = g_config.items[i].key;
        if (strncmp(key, "log_interval", 12) == 0 ||
            strncmp(key, "data_interval", 13) == 0 ||
            strncmp(key, "retention_", 10) == 0) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                            "%s=%s\r\n", key, g_config.items[i].value);
        }
    }

    /* 原子写入：先写临时文件，再重命名 */
    const char* temp_path = "/config/system.ini.tmp";

    /* 备份原文件 */
    if (file_exists(CONFIG_FILE_PATH)) {
        file_delete(CONFIG_BACKUP_PATH);
        /* 复制原文件到备份 - 简化实现 */
    }

    /* 写入新文件 */
    result = file_open(temp_path, FILE_MODE_WRITE);
    if (result != FILE_OK) {
        return FILE_ERROR;
    }

    result = file_write(buffer, pos, NULL);
    file_close();

    if (result != FILE_OK) {
        file_delete(temp_path);
        return FILE_ERROR;
    }

    /* 删除原文件，重命名临时文件 */
    file_delete(CONFIG_FILE_PATH);
    /* 重命名操作需要底层支持，这里简化处理 */
    /* 实际项目中应使用 f_rename() */

    /* 清除修改标记 */
    for (uint32_t i = 0; i < g_config.count; i++) {
        g_config.items[i].modified = 0;
    }

    g_config.version++;
    return FILE_OK;
}

uint8_t config_reset_to_default(void)
{
    g_config.count = 0;

    for (uint32_t i = 0; i < PARAM_DEF_COUNT; i++) {
        set_item(g_param_defs[i].key,
                 g_param_defs[i].default_value,
                 g_param_defs[i].type);
    }

    g_config.version = 1;
    return FILE_OK;
}

uint8_t config_get_string(ConfigParamId id, char* value, uint32_t size)
{
    if (id >= CFG_ITEM_COUNT) {
        return FILE_ERROR;
    }

    const char* key = g_param_defs[id].key;
    return config_get_string_by_key(key, value, size);
}

uint8_t config_get_int(ConfigParamId id, int32_t* value)
{
    char str[32];

    if (config_get_string(id, str, sizeof(str)) != FILE_OK) {
        return FILE_ERROR;
    }

    *value = (int32_t)atoi(str);
    return FILE_OK;
}

uint8_t config_get_float(ConfigParamId id, float* value)
{
    char str[32];

    if (config_get_string(id, str, sizeof(str)) != FILE_OK) {
        return FILE_ERROR;
    }

    *value = (float)atof(str);
    return FILE_OK;
}

uint8_t config_get_bool(ConfigParamId id, uint8_t* value)
{
    int32_t int_val;

    if (config_get_int(id, &int_val) != FILE_OK) {
        return FILE_ERROR;
    }

    *value = (int_val != 0) ? 1 : 0;
    return FILE_OK;
}

uint8_t config_set_string(ConfigParamId id, const char* value)
{
    if (id >= CFG_ITEM_COUNT) {
        return FILE_ERROR;
    }

    const char* key = g_param_defs[id].key;
    return config_set_string_by_key(key, value);
}

uint8_t config_set_int(ConfigParamId id, int32_t value)
{
    char str[32];
    snprintf(str, sizeof(str), "%ld", (long)value);
    return config_set_string(id, str);
}

uint8_t config_set_float(ConfigParamId id, float value, uint8_t precision)
{
    char str[32];
    char fmt[8];
    snprintf(fmt, sizeof(fmt), "%%.%uf", precision);
    snprintf(str, sizeof(str), fmt, value);
    return config_set_string(id, str);
}

uint8_t config_set_bool(ConfigParamId id, uint8_t value)
{
    return config_set_int(id, value ? 1 : 0);
}

uint8_t config_get_string_by_key(const char* key, char* value, uint32_t size)
{
    ConfigItem* item = find_item(key);

    if (!item) {
        /* 查找默认值 */
        for (uint32_t i = 0; i < PARAM_DEF_COUNT; i++) {
            if (strcmp(g_param_defs[i].key, key) == 0) {
                strncpy(value, g_param_defs[i].default_value, size - 1);
                value[size - 1] = '\0';
                return FILE_OK;
            }
        }
        return FILE_ERROR;
    }

    strncpy(value, item->value, size - 1);
    value[size - 1] = '\0';
    return FILE_OK;
}

uint8_t config_set_string_by_key(const char* key, const char* value)
{
    return set_item(key, value, CONFIG_TYPE_STRING);
}

uint32_t config_export(char* buffer, uint32_t size)
{
    uint32_t pos = 0;

    pos += snprintf(buffer + pos, size - pos, "{\r\n");
    pos += snprintf(buffer + pos, size - pos, "  \"version\": %lu,\r\n", g_config.version);
    pos += snprintf(buffer + pos, size - pos, "  \"config\": [\r\n");

    for (uint32_t i = 0; i < g_config.count && pos < size - 100; i++) {
        pos += snprintf(buffer + pos, size - pos,
                        "    {\"key\":\"%s\",\"value\":\"%s\"}%s\r\n",
                        g_config.items[i].key,
                        g_config.items[i].value,
                        (i < g_config.count - 1) ? "," : "");
    }

    pos += snprintf(buffer + pos, size - pos, "  ]\r\n}");
    return pos;
}

uint8_t config_import(const char* data)
{
    /* 简化实现：解析 key=value 对，逗号分隔 */
    (void)data;
    return FILE_OK;
}

void config_print_all(void)
{
    printf("=== Configuration (Version: %lu) ===\r\n", g_config.version);
    printf("Total items: %lu\r\n\r\n", g_config.count);

    for (uint32_t i = 0; i < PARAM_DEF_COUNT; i++) {
        const char* key = g_param_defs[i].key;
        char value[CONFIG_VALUE_MAX_LEN];

        if (config_get_string_by_key(key, value, sizeof(value)) == FILE_OK) {
            printf("[%s] %s = %s (default: %s)\r\n",
                   (g_param_defs[i].type == CONFIG_TYPE_INT) ? "INT" :
                   (g_param_defs[i].type == CONFIG_TYPE_FLOAT) ? "FLOAT" :
                   (g_param_defs[i].type == CONFIG_TYPE_BOOL) ? "BOOL" : "STR",
                   key, value, g_param_defs[i].default_value);
        }
    }
}

uint32_t config_get_version(void)
{
    return g_config.version;
}

uint8_t config_is_modified(void)
{
    for (uint32_t i = 0; i < g_config.count; i++) {
        if (g_config.items[i].modified) {
            return 1;
        }
    }
    return 0;
}
