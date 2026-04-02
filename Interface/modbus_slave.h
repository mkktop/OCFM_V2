/**
 * @file modbus_slave.h
 * @brief Modbus从机头文件 - 响应用户指令
 * @details 通过串口2连接用户设备，响应Modbus指令
 */

#ifndef __MODBUS_SLAVE_H
#define __MODBUS_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "modbus.h"
#include "global.h"

/**
 * @brief Modbus从机状态
 */
typedef enum
{
    MODBUS_SLAVE_STATE_IDLE = 0,        /**< 空闲状态 */
    MODBUS_SLAVE_STATE_RECEIVING,       /**< 接收中 */
    MODBUS_SLAVE_STATE_PROCESSING,      /**< 处理中 */
    MODBUS_SLAVE_STATE_SENDING          /**< 发送中 */
} modbus_slave_state_t;

/**
 * @brief Modbus从机结构体
 */
typedef struct
{
    UART_HandleTypeDef *huart;          /**< 串口句柄 */
    modbus_slave_state_t state;         /**< 当前状态 */
    uint8_t rx_buffer[MODBUS_SLAVE_BUF_SIZE];  /**< 接收缓冲区 */
    uint8_t tx_buffer[MODBUS_SLAVE_BUF_SIZE];  /**< 发送缓冲区 */
    uint16_t rx_length;                 /**< 接收数据长度 */
    uint8_t slave_id;                   /**< 从机ID */
} modbus_slave_t;

/**
 * @brief 全局Modbus从机实例
 */
extern modbus_slave_t sensor_slave;

/**
 * @brief 写入回调函数类型
 * @param start_addr 起始寄存器地址
 * @param quantity 写入的寄存器数量
 * @note 每次写入操作(0x06/0x10)只触发一次回调，
 *       应用层可从 holding_registers[start_addr] 读取写入的值
 */
typedef void (*modbus_write_callback_t)(uint16_t start_addr, uint16_t quantity);

/**
 * @brief 注册写入回调函数
 * @param callback 回调函数指针
 */
void modbus_slave_set_write_callback(modbus_write_callback_t callback);

/**
 * @brief 初始化Modbus从机
 * @param slave 从机结构体指针
 * @param huart 串口句柄指针
 * @note 初始化串口和寄存器数据
 */
void modbus_slave_init(modbus_slave_t *slave, UART_HandleTypeDef *huart);

/**
 * @brief 设置从机ID
 * @param slave 从机结构体指针
 * @param id 从机ID (1-247)
 */
void modbus_slave_set_id(modbus_slave_t *slave, uint8_t id);

/**
 * @brief 串口空闲中断回调函数
 * @param huart 触发中断的串口句柄
 * @param size 接收到的数据字节数
 */
void modbus_slave_rx_idle_callback(UART_HandleTypeDef *huart, uint16_t size);

/**
 * @brief Modbus从机任务处理
 * @param slave 从机结构体指针
 * @note 在主循环中调用，处理接收到的数据并响应
 */
void modbus_slave_task(modbus_slave_t *slave);

/**
 * @brief 处理接收到的数据
 * @param slave 从机结构体指针
 * @param data 接收到的数据
 * @param length 数据长度
 * @return 处理后的响应长度，0表示无需响应
 */
uint16_t modbus_slave_process(modbus_slave_t *slave, uint8_t *data, uint16_t length);

/**
 * @brief 读保持寄存器 (功能码0x03)
 * @param slave 从机结构体指针
 * @param start_addr 起始地址
 * @param quantity 寄存器数量
 * @param response 响应缓冲区
 * @return 响应长度
 */
uint16_t modbus_slave_read_holding_registers(modbus_slave_t *slave, 
                                               uint16_t start_addr, 
                                               uint16_t quantity,
                                               uint8_t *response);

/**
 * @brief 写单个寄存器 (功能码0x06)
 * @param slave 从机结构体指针
 * @param register_addr 寄存器地址
 * @param value 值
 * @param response 响应缓冲区
 * @return 响应长度
 */
uint16_t modbus_slave_write_single_register(modbus_slave_t *slave,
                                              uint16_t register_addr,
                                              uint16_t value,
                                              uint8_t *response);

/**
 * @brief 写多个寄存器 (功能码0x10)
 * @param slave 从机结构体指针
 * @param start_addr 起始地址
 * @param quantity 寄存器数量
 * @param data 数据
 * @param response 响应缓冲区
 * @return 响应长度
 */
uint16_t modbus_slave_write_multiple_registers(modbus_slave_t *slave,
                                                 uint16_t start_addr,
                                                 uint16_t quantity,
                                                 uint8_t *data,
                                                 uint8_t *response);

/**
 * @brief 构建异常响应
 * @param slave 从机结构体指针
 * @param function_code 功能码
 * @param exception_code 异常码
 * @param response 响应缓冲区
 * @return 响应长度
 */
uint16_t modbus_slave_build_exception(modbus_slave_t *slave,
                                        uint8_t function_code,
                                        uint8_t exception_code,
                                        uint8_t *response);

/**
 * @brief 发送响应数据
 * @param slave 从机结构体指针
 * @param data 数据
 * @param length 长度
 */
void modbus_slave_send(modbus_slave_t *slave, uint8_t *data, uint16_t length);

/**
 * @brief 获取保持寄存器值
 * @param addr 寄存器地址
 * @return 寄存器值
 */
uint16_t modbus_slave_get_holding_register(uint16_t addr);

/**
 * @brief 设置保持寄存器值
 * @param addr 寄存器地址
 * @param value 值
 */
void modbus_slave_set_holding_register(uint16_t addr, uint16_t value);

/**
 * @brief 设置32位无符号整数 (占用2个寄存器)
 * @param addr 起始寄存器地址
 * @param value 32位值
 * @note 大端格式: 高位寄存器在前
 */
void modbus_slave_set_uint32(uint16_t addr, uint32_t value);

/**
 * @brief 获取32位无符号整数
 * @param addr 起始寄存器地址
 * @return 32位值
 */
uint32_t modbus_slave_get_uint32(uint16_t addr);

/**
 * @brief 设置32位有符号整数 (占用2个寄存器)
 * @param addr 起始寄存器地址
 * @param value 32位值
 */
void modbus_slave_set_int32(uint16_t addr, int32_t value);

/**
 * @brief 获取32位有符号整数
 * @param addr 起始寄存器地址
 * @return 32位值
 */
int32_t modbus_slave_get_int32(uint16_t addr);

/**
 * @brief 设置浮点数 (占用2个寄存器)
 * @param addr 起始寄存器地址
 * @param value 浮点数值
 * @note IEEE 754格式存储
 */
void modbus_slave_set_float(uint16_t addr, float value);

/**
 * @brief 获取浮点数
 * @param addr 起始寄存器地址
 * @return 浮点数值
 */
float modbus_slave_get_float(uint16_t addr);

/**
 * @brief 设置双精度浮点数 (占用4个寄存器)
 * @param addr 起始寄存器地址
 * @param value 双精度浮点数值
 * @note IEEE 754格式存储
 */
void modbus_slave_set_double(uint16_t addr, double value);

/**
 * @brief 获取双精度浮点数
 * @param addr 起始寄存器地址
 * @return 双精度浮点数值
 */
double modbus_slave_get_double(uint16_t addr);

#ifdef __cplusplus
}
#endif

#endif
