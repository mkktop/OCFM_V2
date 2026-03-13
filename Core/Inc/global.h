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
#define HOLDING_REG_SIZE        32          /* 保持寄存器数量 */
#define MODBUS_SLAVE_BUF_SIZE   256         /* 接收缓冲区大小 */
 




/*============================================================================*/
/*                      Modbus主机配置 (串口1 - 传感器通信)                     */
/*============================================================================*/

/**
 * @brief Modbus主机配置
 */
#define MODBUS_MASTER_TIMEOUT_MS    500     /* 超时时间 (毫秒) */
#define MODBUS_MAX_SLAVE_COUNT      2       /* 最大传感器数量(目前接两路雷达探头)） */
#define MODBUS_MAX_RETRY            3       /* 最大重试次数 */



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
#define REG_AAH                 0x0015       /* 上上限报警值寄存器 占2个寄存器 */
#define REG_AAL                 0x0017       /* 下下限报警值寄存器 占2个寄存器 */

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
#define REG_FACTORY_RANGE       0x1001       /* 出厂量程寄存器 占1个寄存器 */
#define REG_DEAD_ZONE           0x1002       /* 盲区寄存器 占1个寄存器 */
#define REG_DIS_OFFSET          0x1003       /* 距离偏移寄存器 占1个寄存器 */
#define REG_CALIBRATION_4MA     0x1004       /* 4mA校准值寄存器 占1个寄存器 */
#define REG_CALIBRATION_20MA    0x1005       /* 20mA校准值寄存器 占1个寄存器 */
#define REG_FACTORY_SETTING     0x1006       /* 恢复出厂设置寄存器 占1个寄存器 */



/*============================================================================*/
/*                      Modbus主机传感器定义 (串口1 - 传感器通信)                */
/*============================================================================*/

/**
 * @brief 传感器从机地址定义
 */
#define SENSOR_ADDR_1           1       /* 传感器1地址 */
#define SENSOR_ADDR_2           2       /* 传感器2地址 */

/**
 * @brief 传感器寄存器地址定义
 * @note 
 */
#define SENSOR_REG_DATA         0       /* 传感器数据寄存器 */

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
typedef struct
{
    // 基本参数
    uint32_t range_max;         /* 最大量程 */
    uint32_t height;            /* 高度 */
    uint32_t calibration_4ma;   /* 4mA校准值 */
    uint32_t calibration_20ma;  /* 20mA校准值 */
    uint32_t range_4ma;         /* 4mA量程 */
    uint32_t range_20ma;        /* 20mA量程 */
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
    uint32_t alarm_ah;          /* 上限报警 */
    uint32_t alarm_al;          /* 下限报警 */
    uint32_t alarm_dh;          /* 上限报警回差 */
    uint32_t alarm_dl;          /* 下限报警回差 */
    uint32_t alarm_aah;         /* 上上限报警 */
    uint32_t alarm_aal;         /* 下下限报警 */
    // 其他参数
    uint32_t factory_settings;  /* 恢复出厂设置 */
    uint32_t dis_offset;        /* 偏移量 */
    uint32_t canals_type;       /* 水渠类型 */
    uint32_t channel_id;        /* 通道编号 */
    uint32_t instant_unit;      /* 瞬时流量单位 */
    uint32_t sum_point;        /* 累计流量小数点数 */
    uint32_t language;          /* 语言 */
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
    FLOW_UNIT_L_S = 0,     /* 升/秒 (基准单位) */
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
    PARSHALL_FLUME = 0,       /* 巴歇尔水槽 */
    TRIANGULAR_WEIR,          /* 三角堰 */
    RECTANGULAR_WEIR,         /* 矩形堰 */
} canals_type_t;


#endif
