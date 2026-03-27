/**
 * @file modbus_master.h
 * @brief Modbus主机头文件 - 用于轮询485传感器
 * @details 通过串口1连接485传感器，使用空闲中断+DMA接收数据
 */

#ifndef __MODBUS_MASTER_H
#define __MODBUS_MASTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "modbus.h"
#include "global.h"

/**
 * @brief Modbus主机超时时间 (ms)
 */
#define MODBUS_MASTER_TIMEOUT_MS    500

/**
 * @brief 最大重试次数 (约30秒判定离线)
 */
#define MODBUS_MAX_RETRY            20

/**
 * @brief Modbus主机状态枚举
 */
typedef enum
{
    MODBUS_MASTER_STATE_IDLE = 0,           /**< 空闲状态 */
    MODBUS_MASTER_STATE_SENDING,            /**< 发送中 */
    MODBUS_MASTER_STATE_WAITING_RESPONSE,   /**< 等待响应 */
    MODBUS_MASTER_STATE_RECEIVING,          /**< 接收中 */
    MODBUS_MASTER_STATE_PROCESSING,         /**< 处理中 */
    MODBUS_MASTER_STATE_ERROR               /**< 错误状态 */
} modbus_master_state_t;

/**
 * @brief 传感器设备结构体
 */
typedef struct
{
    uint8_t slave_id;           /**< 从机ID (1-247) */
    uint16_t start_addr;        /**< 寄存器起始地址 */
    uint16_t quantity;          /**< 寄存器数量 */
    uint8_t retry_count;        /**< 当前重试次数 */
    uint32_t last_poll_time;    /**< 上次成功轮询的时间戳 */
    uint8_t is_active;          /**< 传感器是否在线 (1:在线 0:离线) */
    uint8_t data[64];           /**< 接收到的数据缓冲区 */
    uint16_t data_length;       /**< 接收到的数据长度 */
} modbus_sensor_t;

/**
 * @brief Modbus主机结构体
 */
typedef struct
{
    UART_HandleTypeDef *huart;              /**< 串口句柄 */
    modbus_master_state_t state;            /**< 当前状态 */
    uint8_t tx_buffer[256];                 /**< 发送缓冲区 */
    uint8_t rx_buffer[256];                 /**< 接收缓冲区 */
    uint16_t tx_length;                     /**< 发送数据长度 */
    uint16_t rx_length;                     /**< 接收数据长度 */
    uint32_t timeout_start;                 /**< 超时起始时间 */
    uint8_t current_slave_index;            /**< 当前轮询的传感器索引 */
    modbus_sensor_t sensors[MODBUS_MAX_SLAVE_COUNT];  /**< 传感器数组 */
    uint8_t sensor_count;                   /**< 已配置的传感器数量 */
} modbus_master_t;

/**
 * @brief 全局Modbus主机实例
 */
extern modbus_master_t sensor_master;

/*============================================================================*/
/*                           初始化与配置                                       */
/*============================================================================*/

/**
 * @brief 初始化Modbus主机
 * @param master 主机结构体指针
 * @param huart 串口句柄指针
 */
void modbus_master_init(modbus_master_t *master, UART_HandleTypeDef *huart);

/**
 * @brief 添加传感器设备
 * @param master 主机结构体指针
 * @param slave_id 从机ID (1-247)
 * @param start_addr 寄存器起始地址
 * @param quantity 寄存器数量
 * @return 1:成功 0:失败
 */
uint8_t modbus_master_add_sensor(modbus_master_t *master, uint8_t slave_id,
                                  uint16_t start_addr, uint16_t quantity);

/*============================================================================*/
/*                           轮询任务                                          */
/*============================================================================*/

/**
 * @brief Modbus主机轮询任务
 * @param master 主机结构体指针
 * @note 非阻塞方式，需在主循环中周期性调用
 */
void modbus_master_poll(modbus_master_t *master);

/*============================================================================*/
/*                           收发函数                                          */
/*============================================================================*/

/**
 * @brief 发送Modbus数据帧
 * @param master 主机结构体指针
 * @param data 数据缓冲区
 * @param length 数据长度
 */
void modbus_master_send_frame(modbus_master_t *master, uint8_t *data, uint16_t length);

/**
 * @brief 处理Modbus响应数据
 * @param master 主机结构体指针
 * @param data 响应数据缓冲区
 * @param length 数据长度
 * @return 1:成功 0:失败
 */
uint8_t modbus_master_process_response(modbus_master_t *master, uint8_t *data, uint16_t length);

/*============================================================================*/
/*                           数据获取函数                                       */
/*============================================================================*/

/**
 * @brief 获取传感器值
 * @param sensor_index 传感器索引
 * @param value 值输出指针
 * @return 1:成功 0:失败
 */
uint8_t modbus_master_get_sensor_value(uint8_t sensor_index, uint16_t *value);

/**
 * @brief 获取传感器数据
 * @param sensor_index 传感器索引
 * @return 传感器数据缓冲区指针
 */
uint8_t* modbus_master_get_sensor_data(uint8_t sensor_index);

/**
 * @brief 获取传感器数据长度
 * @param sensor_index 传感器索引
 * @return 数据长度
 */
uint16_t modbus_master_get_sensor_data_length(uint8_t sensor_index);

/**
 * @brief 检查传感器是否在线
 * @param sensor_index 传感器索引
 * @return 1:在线 0:离线
 */
uint8_t modbus_master_is_sensor_online(uint8_t sensor_index);

/**
 * @brief 获取寄存器值
 * @param sensor_index 传感器索引
 * @param register_index 寄存器索引 (从0开始)
 * @return 寄存器值 (16位)
 */
uint16_t modbus_master_get_register_value(uint8_t sensor_index, uint8_t register_index);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_MASTER_H */
