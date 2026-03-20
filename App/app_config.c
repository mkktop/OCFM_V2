/**
 * @file app_config.c
 * @brief 系统配置管理源文件
 */

#include "app_config.h"
#include "at24c02.h"
#include <string.h>

/* 全局配置实例 */
static SystemConfig_t g_config;

/**
 * @brief 使用默认值初始化配置
 * @retval None
 */
void app_config_set_default(void)
{
    memset(&g_config, 0, sizeof(SystemConfig_t));

    g_config.magic_number = CONFIG_MAGIC_NUMBER;

    // 基本参数
    g_config.range_max         = DEFAULT_RANGE_MAX;
    g_config.height            = DEFAULT_HEIGHT;
    g_config.calibration_4ma   = DEFAULT_CALIBRATION_4MA;
    g_config.calibration_20ma  = DEFAULT_CALIBRATION_20MA;
    g_config.range_4ma         = DEFAULT_RANGE_4MA;
    g_config.range_20ma        = DEFAULT_RANGE_20MA;
    g_config.point_num         = DEFAULT_POINT_NUM;

    // 测量参数
    g_config.window_width      = DEFAULT_WINDOW_WIDTH;
    g_config.filter_count      = DEFAULT_FILTER_COUNT;
    g_config.delay_time        = DEFAULT_DELAY_TIME;
    g_config.antenna_type      = DEFAULT_ANTENNA_TYPE;
    g_config.blind_area        = DEFAULT_BLIND_AREA;
    g_config.w_coeff           = DEFAULT_W_COEFF;
    g_config.m_coeff           = DEFAULT_M_COEFF;

    // Modbus从机参数
    g_config.modbusAddr        = DEFAULT_MODBUS_ADDR;
    g_config.modbusBaudRate    = DEFAULT_MODBUS_BAUD;
    g_config.modbusStopBits    = DEFAULT_MODBUS_STOP;

    // 报警参数
    g_config.alarm_ah          = DEFAULT_ALARM_AH;
    g_config.alarm_al          = DEFAULT_ALARM_AL;
    g_config.alarm_dh          = DEFAULT_ALARM_DH;
    g_config.alarm_dl          = DEFAULT_ALARM_DL;
    g_config.alarm_aah         = DEFAULT_ALARM_AAH;
    g_config.alarm_aal         = DEFAULT_ALARM_AAL;

    // 其他参数
    g_config.factory_settings  = DEFAULT_FACTORY_SETTINGS;
    g_config.dis_offset        = DEFAULT_DIS_OFFSET;
    g_config.canals_type       = DEFAULT_CANALS_TYPE;
    g_config.channel_id        = DEFAULT_CHANNEL_ID;
    g_config.instant_unit      = DEFAULT_INSTANT_UNIT;
    g_config.sum_point         = DEFAULT_SUM_POINT;
    g_config.language          = DEFAULT_LANGUAGE;
}

/**
 * @brief 保存系统配置到EEPROM
 * @retval 1: 成功 0: 失败
 */
uint8_t app_config_save(void)
{
    /* 确保magic_number已设置 */
    g_config.magic_number = CONFIG_MAGIC_NUMBER;

    /* 写入EEPROM */
    return at24c02_write_buffer(CONFIG_EEPROM_ADDR, sizeof(SystemConfig_t),
                                (uint8_t*)&g_config);
}

/**
 * @brief 加载系统配置从EEPROM
 * @retval 1: 成功 0: 失败
 */
uint8_t app_config_load(void)
{
    return at24c02_read_buffer(CONFIG_EEPROM_ADDR, sizeof(SystemConfig_t),
                               (uint8_t*)&g_config);
}

/**
 * @brief 检查配置是否有效
 * @retval 1: 有效 0: 无效
 */
uint8_t app_config_is_valid(void)
{
    return (g_config.magic_number == CONFIG_MAGIC_NUMBER) ? 1 : 0;
}

/**
 * @brief 初始化系统配置
 * @retval None
 * @note 从EEPROM读取配置，若无效则使用默认值初始化
 */
void app_config_init(void)
{
    /* 从EEPROM加载配置 */
    if (app_config_load() == 1) {
        /* 检查配置是否有效 */
        if (app_config_is_valid() == 1) {
            /* 配置有效，直接使用 */
            return;
        }
    }

    /* EEPROM无数据或配置无效，使用默认值 */
    app_config_set_default();

    /* 保存默认值到EEPROM */
    app_config_save();
}

/**
 * @brief 获取系统配置指针
 * @retval SystemConfig_t结构体指针
 */
SystemConfig_t* app_config_get(void)
{
    return &g_config;
}

/**
 * @brief 恢复出厂设置
 * @retval 1: 成功 0: 失败
 */
uint8_t app_config_factory_reset(void)
{
    /* 使用默认值初始化 */
    app_config_set_default();

    /* 保存到EEPROM */
    return app_config_save();
}

/*============================================================================*/
/*                           基本参数 Getter/Setter                             */
/*============================================================================*/
// 最大量程
uint32_t app_config_get_range_max(void)
{
    return g_config.range_max;
}

void app_config_set_range_max(uint32_t value)
{
    g_config.range_max = value;
}

// 高度
uint32_t app_config_get_height(void)
{
    return g_config.height;
}

void app_config_set_height(uint32_t value)
{
    g_config.height = value;
}

// 4mA校准值
uint32_t app_config_get_calibration_4ma(void)
{
    return g_config.calibration_4ma;
}

void app_config_set_calibration_4ma(uint32_t value)
{
    g_config.calibration_4ma = value;
}

// 20mA校准值
uint32_t app_config_get_calibration_20ma(void)
{
    return g_config.calibration_20ma;
}

void app_config_set_calibration_20ma(uint32_t value)
{
    g_config.calibration_20ma = value;
}

// 4mA量程
uint32_t app_config_get_range_4ma(void)
{
    return g_config.range_4ma;
}

void app_config_set_range_4ma(uint32_t value)
{
    g_config.range_4ma = value;
}

// 20mA量程
uint32_t app_config_get_range_20ma(void)
{
    return g_config.range_20ma;
}

void app_config_set_range_20ma(uint32_t value)
{
    g_config.range_20ma = value;
}

// 小数点数量
uint32_t app_config_get_point_num(void)
{
    return g_config.point_num;
}

void app_config_set_point_num(uint32_t value)
{
    g_config.point_num = value;
}

/*============================================================================*/
/*                           测量参数 Getter/Setter                             */
/*============================================================================*/
// 窗口宽度
uint32_t app_config_get_window_width(void)
{
    return g_config.window_width;
}

void app_config_set_window_width(uint32_t value)
{
    g_config.window_width = value;
}

// 滤波次数
uint32_t app_config_get_filter_count(void)
{
    return g_config.filter_count;
}

void app_config_set_filter_count(uint32_t value)
{
    g_config.filter_count = value;
}

// 传感器数据采集延迟时间
uint32_t app_config_get_delay_time(void)
{
    return g_config.delay_time;
}

void app_config_set_delay_time(uint32_t value)
{
    g_config.delay_time = value;
}

// 天线类型
uint32_t app_config_get_antenna_type(void)
{
    return g_config.antenna_type;
}

void app_config_set_antenna_type(uint32_t value)
{
    g_config.antenna_type = value;
}

// 测量盲区
uint32_t app_config_get_blind_area(void)
{
    return g_config.blind_area;
}

void app_config_set_blind_area(uint32_t value)
{
    g_config.blind_area = value;
}

// 窗口系数
uint32_t app_config_get_w_coeff(void)
{
    return g_config.w_coeff;
}

void app_config_set_w_coeff(uint32_t value)
{
    g_config.w_coeff = value;
}

// 测量系数
uint32_t app_config_get_m_coeff(void)
{
    return g_config.m_coeff;
}

void app_config_set_m_coeff(uint32_t value)
{
    g_config.m_coeff = value;
}

/*============================================================================*/
/*                           Modbus参数 Getter/Setter                           */
/*============================================================================*/
// Modbus从机地址
uint32_t app_config_get_modbus_addr(void)
{
    return g_config.modbusAddr;
}

void app_config_set_modbus_addr(uint32_t value)
{
    g_config.modbusAddr = value;
}

// Modbus从机波特率
uint32_t app_config_get_modbus_baudrate(void)
{
    return g_config.modbusBaudRate;
}

void app_config_set_modbus_baudrate(uint32_t value)
{
    g_config.modbusBaudRate = value;
}

// Modbus从机停止位
uint32_t app_config_get_modbus_stopbits(void)
{
    return g_config.modbusStopBits;
}

void app_config_set_modbus_stopbits(uint32_t value)
{
    g_config.modbusStopBits = value;
}

/*============================================================================*/
/*                           报警参数 Getter/Setter                             */
/*============================================================================*/
// 上限报警
uint32_t app_config_get_alarm_ah(void)
{
    return g_config.alarm_ah;
}

void app_config_set_alarm_ah(uint32_t value)
{
    g_config.alarm_ah = value;
}

// 下限报警
uint32_t app_config_get_alarm_al(void)
{
    return g_config.alarm_al;
}

void app_config_set_alarm_al(uint32_t value)
{
    g_config.alarm_al = value;
}

// 上限报警回差
uint32_t app_config_get_alarm_dh(void)
{
    return g_config.alarm_dh;
}

void app_config_set_alarm_dh(uint32_t value)
{
    g_config.alarm_dh = value;
}

// 下限报警回差
uint32_t app_config_get_alarm_dl(void)
{
    return g_config.alarm_dl;
}

void app_config_set_alarm_dl(uint32_t value)
{
    g_config.alarm_dl = value;
}

// 上上限报警
uint32_t app_config_get_alarm_aah(void)
{
    return g_config.alarm_aah;
}

void app_config_set_alarm_aah(uint32_t value)
{
    g_config.alarm_aah = value;
}

// 下下限报警
uint32_t app_config_get_alarm_aal(void)
{
    return g_config.alarm_aal;
}

void app_config_set_alarm_aal(uint32_t value)
{
    g_config.alarm_aal = value;
}

/*============================================================================*/
/*                           其他参数 Getter/Setter                             */
/*============================================================================*/
// 恢复出厂设置
uint32_t app_config_get_factory_settings(void)
{
    return g_config.factory_settings;
}

void app_config_set_factory_settings(uint32_t value)
{
    g_config.factory_settings = value;
}

// 偏移量
uint32_t app_config_get_dis_offset(void)
{
    return g_config.dis_offset;
}

void app_config_set_dis_offset(uint32_t value)
{
    g_config.dis_offset = value;
}

// 水渠类型
uint32_t app_config_get_canals_type(void)
{
    return g_config.canals_type;
}

void app_config_set_canals_type(uint32_t value)
{
    g_config.canals_type = value;
}

// 通道编号
uint32_t app_config_get_channel_id(void)
{
    return g_config.channel_id;
}

void app_config_set_channel_id(uint32_t value)
{
    g_config.channel_id = value;
}

// 瞬时流量单位
uint32_t app_config_get_instant_unit(void)
{
    return g_config.instant_unit;
}

void app_config_set_instant_unit(uint32_t value)
{
    g_config.instant_unit = value;
}

// 累计流量小数点数
uint32_t app_config_get_sum_point(void)
{
    return g_config.sum_point;
}

void app_config_set_sum_point(uint32_t value)
{
    g_config.sum_point = value;
}

// 语言
uint32_t app_config_get_language(void)
{
    return g_config.language;
}

void app_config_set_language(uint32_t value)
{
    g_config.language = value;
}

