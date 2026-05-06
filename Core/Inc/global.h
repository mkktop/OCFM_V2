/**
 * @file global.h
 * @author mkk
 * @brief 全局配置文件 - 集中管理所有宏定义和配置
 */

#ifndef __GLOBAL_H
#define __GLOBAL_H
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "at24c02.h"
#include "fsmc_st7789.h"
#include "fatfs.h"
#include "file_driver.h"
#include "data_recorder.h"
#include "log_manager.h"
#include "rtc_time.h"
#include "ui/ui.h"
/*============================================================================*/
/*                           系统信息配置                                          */
/*============================================================================*/

/**
 * @brief 设备信息
 */
#define FIRMWARE_VERSION        0x0100      /* 固件版本 v1.00 */

/*============================================================================*/
/*                      Modbus从机配置 (串口2 - 与用户进行通信)                       */
/*============================================================================*/

/**
 * @brief Modbus从机配置
 */
#define HOLDING_REG_SIZE        128         /* 保持寄存器数量 (支持地址0x0000-0x1006) */
#define MODBUS_SLAVE_BUF_SIZE   256         /* 接收缓冲区大小 */
#define MODBUS_SLAVE_ID         1           /* 本机从机地址 (UART2用户通信) */
 




/*============================================================================*/
/*                      Modbus主机配置 (串口1 - 传感器通信)                     */
/*============================================================================*/

/**
 * @brief Modbus主机配置
 */
#define MODBUS_MASTER_TIMEOUT_MS    500     /* 超时时间 (毫秒) */
#define MODBUS_MAX_SLAVE_COUNT      2       /* 最大传感器数量(目前接两路雷达探头)） */
#define MODBUS_MAX_RETRY            10      /* 最大重试次数 */



/**
 * @brief 寄存器地址定义
 * @note 所有数据使用保持寄存器，地址从1开始
 *       uint16_t占1个寄存器，float占2个寄存器，double占4个寄存器，int32占2个寄存器
 */

/*----------------------------------------------------------------------------*/
/* 传感器数据区 (设备更新，用户读取)                                            */
/*----------------------------------------------------------------------------*/
//基础数据寄存器
#define REG_WUWEI               0x0001       /* 物位寄存器 占1个寄存器 */
#define REG_DISTANCE            0x0002       /* 距离寄存器 占1个寄存器 */
#define REG_TEMPERATURE         0x0003       /* 温度寄存器 占1个寄存器 */
#define REG_INSTANT_FLOW        0x0004       /* 瞬时流量寄存器 占2个寄存器 */
#define REG_SUM_FLOW            0x0006       /* 累计流量寄存器 占4个寄存器 */
#define REG_RELAY1_STATUS       0x000A       /* 继电器1状态寄存器 占1个寄存器 */
#define REG_RELAY2_STATUS       0x000B       /* 继电器2状态寄存器 占1个寄存器 */
#define REG_RELAY3_STATUS       0x000C       /* 继电器3状态寄存器 占1个寄存器 */
#define REG_RELAY4_STATUS       0x000D       /* 继电器4状态寄存器 占1个寄存器 */

//报警值寄存器
#define REG_AH                  0x000E       /* 上限报警值寄存器 占2个寄存器 */
#define REG_DH                  0x0010       /* 上限回差寄存器 占2个寄存器 */
#define REG_AL                  0x0012       /* 下限报警值寄存器 占2个寄存器 */
#define REG_DL                  0x0014       /* 下限回差寄存器 占2个寄存器 */
#define REG_AAH                 0x0016       /* 上上限报警值寄存器 占2个寄存器 */
#define REG_AAL                 0x0018       /* 下下限报警值寄存器 占2个寄存器 */

//传感器参数设置寄存器
#define REG_RANGE_MAX           0x0065       /* 最大量程寄存器 占1个寄存器 */
#define REG_HEIGHT              0x0066       /* 高度寄存器 占1个寄存器 */
#define REG_L1                  0x0067       /* L1(window_width)寄存器 占1个寄存器 */
#define REG_L2                  0x0068       /* L2(filt_Count)寄存器 占1个寄存器 */
#define REG_L3                  0x0069       /* L3(delaytime)寄存器 占1个寄存器 */
#define REG_L4                  0x006A       /* L4(blind_area)寄存器 占1个寄存器 */
#define REG_L5                  0x006B       /* L5(w_coeff)寄存器 占1个寄存器 */
#define REG_L6                  0x006C       /* L6(m_coeff)寄存器 占1个寄存器 */
#define REG_ADDRESS             0x006D       /* 传感器地址寄存器 占1个寄存器 */
#define REG_BAUDE_RATE          0x006E       /* 波特率寄存器 占1个寄存器 */
#define REG_STOP_BITS           0x006F       /* 停止位寄存器 占1个寄存器 */

//Modbus从机参数寄存器
#define REG_CANALS__TYPE        0x0101       /* 渠道类型寄存器 占1个寄存器 */
#define REG_CHANNEL_ID          0x0102       /* 水槽编号寄存器 占1个寄存器 */
#define REG_INSTANT_UNIT        0x0103       /* 瞬时流量单位寄存器 占1个寄存器 */
#define REG_SUM_POINT           0x0104       /* 累计流量小数位数寄存器 占1个寄存器 */
#define REG_RANGE_4MA           0x0105       /* 4mA量程寄存器 占2个寄存器 */
#define REG_RANGE_20MA          0x0107       /* 20mA量程寄存器 占2个寄存器 */

//出厂校准寄存器
#define REG_DEAD_ZONE           0x1001       /* 天线类型寄存器 占1个寄存器 */
#define REG_DIS_OFFSET          0x1002       /* 距离偏移寄存器 占1个寄存器 */
#define REG_CALIBRATION_4MA     0x1003       /* 4mA校准值寄存器 占1个寄存器 */
#define REG_CALIBRATION_20MA    0x1004       /* 20mA校准值寄存器 占1个寄存器 */
#define REG_FACTORY_SETTING     0x1005       /* 恢复出厂设置寄存器 占1个寄存器 */
#define REG_CLEAR_TOTAL         0x1006       /* 清除累计(流量+时间)寄存器 占1个寄存器 (写1触发) */

//RTC时间设置寄存器
#define REG_RTC_YEAR            0x0200       /* 年 (2000-2099) 占1个寄存器 */
#define REG_RTC_MONTH           0x0201       /* 月 (1-12)       占1个寄存器 */
#define REG_RTC_DAY             0x0202       /* 日 (1-31)       占1个寄存器 */
#define REG_RTC_HOUR            0x0203       /* 时 (0-23)       占1个寄存器 */
#define REG_RTC_MINUTE          0x0204       /* 分 (0-59)       占1个寄存器 */
#define REG_RTC_SECOND          0x0205       /* 秒 (0-59)       占1个寄存器 */
#define REG_RTC_WEEKDAY         0x0206       /* 星期 (1-7)      占1个寄存器 */



/*============================================================================*/
/*                      Modbus主机传感器定义 (串口1 - 传感器通信)                */
/*============================================================================*/

/**
 * @brief 传感器从机地址定义
 */
#define SENSOR_ADDR_1           1       /* 传感器1地址 */
#define SENSOR_ADDR_2           2       /* 传感器2地址 */

/* 传感器寄存器地址 */
#define SENSOR_REG_DISTANCE     0x0005  /* 距离 */
#define SENSOR_REG_QUANTITY     1       /* 轮询寄存器数量 */

/* 传感器配置寄存器 (写入) */
#define SENSOR_COM_GAODU        0x0066  /* 高度 */
#define SENSOR_COM_L1           0x0067  /* 窗口宽度 */
#define SENSOR_COM_L2           0x0068  /* 滤波次数 */
#define SENSOR_COM_L3           0x0069  /* 延时时间 */
#define SENSOR_COM_L4           0x006A  /* 盲区 */
#define SENSOR_COM_L5           0x006B  /* W系数 */
#define SENSOR_COM_L6           0x006C  /* M系数 */
#define SENSOR_COM_FACTORY_LC   0x1000  /* 满量程（出厂量程） */
#define SENSOR_COM_DEAD_ZONE    0x1001  /* 死区/天线类型 */
#define SENSOR_COM_DIS_OFFSET  0x1002  /* 距离偏移 */
#define SENSOR_COM_RESET_FLAG   0x1003  /* 复位标志 */

/**
 * @brief 传感器寄存器地址定义
 * @note 
 */
#define SENSOR_REG_DATA         0       /* 传感器数据寄存器 */

/*============================================================================*/
/*                           数据记录配置                                       */
/*============================================================================*/

/**
 * @brief 数据记录间隔 (毫秒)
 */
#define DATA_RECORD_INTERVAL_MS   60000   /* 1分钟 */

/*============================================================================*/
/*                           状态标志定义                                       */
/*============================================================================*/

/**
 * @brief 错误码定义
 */
#define ERROR_NONE              0       /* 无错误 */
#define ERROR_SENSOR_TIMEOUT    1       /* 传感器超时 */
#define ERROR_SENSOR_CRC        2       /* 传感器CRC错误 */


/*============================================================================*/
/*                           系统参数结构体                                          */
/*============================================================================*/

/**
 * @brief 配置有效标志
 * @note 用于检测EEPROM中是否已写入有效配置
 */
#define CONFIG_MAGIC_NUMBER    0xA5A5A5A5U

/**
 * @brief 配置在EEPROM中的起始地址
 */
#define CONFIG_EEPROM_ADDR     0

/**
 * @brief 累计流量在EEPROM中的存储地址 (最后一页)
 * @note 与配置区分开，避免频繁写入影响配置区寿命
 */
#define TOTAL_FLOW_EEPROM_ADDR     240

/**
 * @brief 累计流量存储校验标志
 */
#define TOTAL_FLOW_MAGIC_NUMBER    0x5A5A5A5AU

typedef struct
{
    uint32_t magic_number;      /* 配置有效标志 */

    // 基本参数
    uint32_t range_max;         /* 最大量程 */
    uint32_t height;            /* 高度 */
    uint32_t calibration_4ma;   /* 4mA校准值 */
    uint32_t calibration_20ma;  /* 20mA校准值 */
    float range_4ma;            /* 4mA量程 (m³/h) */
    float range_20ma;           /* 20mA量程 (m³/h) */
    uint32_t point_num;         /* 小数点数量 */
    // 测量参数
    uint32_t window_width;      /* 窗口宽度 */
    uint32_t filter_count;      /* 滤波次数 */
    uint32_t delay_time;        /* 传感器数据采集延迟时间 (毫秒) */
    uint32_t antenna_type;      /* 天线类型 */
    uint32_t blind_area;        /* 测量盲区 */
    uint32_t w_coeff;           /* 窗口系数 */
    uint32_t m_coeff;           /* 测量系数 */
    // Modbus从机参数
    uint32_t modbusAddr;        /* Modbus从机地址 */
    uint32_t modbusBaudRate;    /* Modbus从机波特率 */
    uint32_t modbusStopBits;    /* Modbus从机停止位 */
    // 报警参数
    float alarm_ah;             /* 上限报警 (m³/h) */
    float alarm_al;             /* 下限报警 (m³/h) */
    float alarm_dh;             /* 上限报警回差 (m³/h) */
    float alarm_dl;             /* 下限报警回差 (m³/h) */
    float alarm_aah;            /* 上上限报警 (m³/h) */
    float alarm_aal;            /* 下下限报警 (m³/h) */
    // 其他参数
    uint32_t factory_settings;  /* 恢复出厂设置 */
    uint32_t dis_offset;        /* 偏移量 */
    uint32_t canals_type;       /* 水渠类型 */
    uint32_t channel_id;        /* 通道编号 */
    uint32_t instant_unit;      /* 瞬时流量单位 */
    uint32_t sum_point;         /* 累计流量小数点数 */
    uint32_t language;          /* 语言 */
    uint32_t show_alarm;        /* 是否显示报警 (1:显示 0:不显示) */
    uint32_t password_enable;   /* 是否启用密码锁 (1:启用 0:禁用) */
} SystemConfig_t;

/*============================================================================*/
/*                           流量单位定义                                       */
/*============================================================================*/

/**
 * @brief 流量单位类型枚举
 * @note  内部计算统一使用 L/s 作为基准单位
 */
typedef enum
{
    FLOW_UNIT_L_S = 1,     /* 升/秒 (基准单位) */
    FLOW_UNIT_L_MIN,       /* 升/分钟 */
    FLOW_UNIT_L_H,         /* 升/小时 */
    FLOW_UNIT_M3_H,        /* 立方米/小时 */
    FLOW_UNIT_M3_S,        /* 立方米/秒 */
    FLOW_UNIT_M3_MIN,      /* 立方米/分钟 */
    FLOW_UNIT_T_H,         /* 吨/小时 */
    FLOW_UNIT_G_H,         /* 美制加仑/小时 */
} flow_unit_t;

/*============================================================================*/
/*                           水渠类型定义                                       */
/*============================================================================*/

/**
 * @brief 水渠类型枚举
 */
typedef enum
{
    PARSHALL_FLUME = 1,       /* 巴歇尔水槽 */
    TRIANGULAR_WEIR,          /* 三角堰 */
    RECTANGULAR_WEIR,         /* 矩形堰 */
} canals_type_t;

/*============================================================================*/
/*                           系统配置默认值                                      */
/*============================================================================*/

/*
 * 恢复出厂设置开关
 * 1: 启动时恢复默认值并保存到EEPROM
 * 0: 正常从EEPROM加载配置
 * 注意：恢复完成后应改回0
 */
#define CONFIG_FACTORY_RESET    0

// 基本参数默认值
#define DEFAULT_RANGE_MAX        5000       /* 最大量程 5000mm */
#define DEFAULT_HEIGHT           5000        /* 高度 5000mm */
#define DEFAULT_CALIBRATION_4MA  1150        /* 4mA校准值 */
#define DEFAULT_CALIBRATION_20MA 4355       /* 20mA校准值 */
#define DEFAULT_RANGE_4MA        0.0f        /* 4mA量程 (m³/h) */
#define DEFAULT_RANGE_20MA       19.354f    /* 20mA量程 (m³/h) */
#define DEFAULT_POINT_NUM        3           /* 小数点数量 */

// 测量参数默认值
#define DEFAULT_WINDOW_WIDTH     100         /* 窗口宽度 500mm */
#define DEFAULT_FILTER_COUNT     2           /* 滤波次数 */
#define DEFAULT_DELAY_TIME       50         /* 延迟时间 100ms */
#define DEFAULT_ANTENNA_TYPE     1           /* 天线类型 */
#define DEFAULT_BLIND_AREA       50         /* 盲区 300mm */
#define DEFAULT_W_COEFF          8          /* 窗口系数 */
#define DEFAULT_M_COEFF          3           /* 测量系数 */

// Modbus从机参数默认值
#define DEFAULT_MODBUS_ADDR      1           /* Modbus地址 */
#define DEFAULT_MODBUS_BAUD      2           /* 波特率索引 (2=9600) */
#define DEFAULT_MODBUS_STOP      1           /* 停止位索引 */

// 报警参数默认值
#define DEFAULT_ALARM_AH         19.44f     /* 上限报警 (m³/h) */
#define DEFAULT_ALARM_AL         0.0f      /* 下限报警 (m³/h) */
#define DEFAULT_ALARM_DH         2.0f      /* 上限回差 (m³/h) */
#define DEFAULT_ALARM_DL         2.0f       /* 下限回差 (m³/h) */
#define DEFAULT_ALARM_AAH        100.0f     /* 上上限报警 (m³/h) */
#define DEFAULT_ALARM_AAL        0.0f      /* 下下限报警 (m³/h) */

// 其他参数默认值
#define DEFAULT_FACTORY_SETTINGS 0           /* 恢复出厂设置 */
#define DEFAULT_DIS_OFFSET       10           /* 偏移量 */
#define DEFAULT_CANALS_TYPE      1           /* 水渠类型 (巴歇尔槽) */
#define DEFAULT_CHANNEL_ID       1           /* 通道编号 */
#define DEFAULT_INSTANT_UNIT     4           /* 瞬时流量单位 (m³/h) */
#define DEFAULT_SUM_POINT        1           /* 累计流量小数点数 */
#define DEFAULT_LANGUAGE         1           /* 语言 (中文) */
#define DEFAULT_SHOW_ALARM       1           /* 显示报警 (默认显示) */
#define DEFAULT_PASSWORD_ENABLE  0           /* 密码锁 (默认关闭) */

#endif
