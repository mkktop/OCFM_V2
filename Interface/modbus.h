/**
 * @file modbus.h
 * @brief Modbus核心定义和函数声明
 * @details 定义Modbus协议常用的功能码、异常码和数据结构
 */

#ifndef __MODBUS_H
#define __MODBUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Modbus协议类型
 */
#define MODBUS_RTU     0   /**< RTU模式 (二进制, 高效) */
#define MODBUS_ASCII   1   /**< ASCII模式 (可读性好, 效率低) */

/**
 * @brief Modbus功能码定义
 * @note 只保留保持寄存器相关功能码
 */
#define MODBUS_FUNC_READ_HOLDING        0x03  /**< 读保持寄存器 (Read Holding Registers) */
#define MODBUS_FUNC_WRITE_SINGLE_REG    0x06  /**< 写单个寄存器 (Write Single Register) */
#define MODBUS_FUNC_WRITE_MULTIPLE_REG  0x10  /**< 写多个寄存器 (Write Multiple Registers) */

/**
 * @brief Modbus异常码定义
 * @note 当从机无法处理请求时返回异常响应
 */
#define MODBUS_EX_ILLEGAL_FUNCTION    0x01  /**< 非法功能码 */
#define MODBUS_EX_ILLEGAL_DATA_ADDR   0x02  /**< 非法数据地址 */
#define MODBUS_EX_ILLEGAL_DATA_VALUE  0x03  /**< 非法数据值 */
#define MODBUS_EX_SERVER_FAILURE      0x04  /**< 从机设备故障 */

/**
 * @brief Modbus数据结构体
 * @note 通用Modbus帧结构，用于解析和构建Modbus数据
 */
typedef struct
{
    uint8_t  u8id;              /**< 从机ID (1-247) */
    uint8_t  u8fct;             /**< 功能码 */
    uint16_t u16RegAdd;         /**< 寄存器地址 */
    uint16_t u16CoilsNo;        /**< 线圈/寄存器数量 */
    uint16_t u16RegNum;         /**< 寄存器数量 */
    uint8_t  au8data[256];      /**< 数据缓冲区 */
    uint16_t u16CRC;            /**< CRC校验码 */
    uint8_t  u8len;             /**< 数据长度 */
    uint8_t  u8state;           /**< 状态 */
    int8_t   i8state;           /**< 详细状态 */
    int32_t  i32timeout;        /**< 超时时间 (毫秒) */
    uint32_t u32timeStart;      /**< 超时起始时间 */
    uint8_t  au8buffer[256];    /**< 原始数据缓冲区 */
}__attribute__((packed)) modbus_t;

/**
 * @brief 计算Modbus CRC16校验码
 * @param buffer 数据缓冲区
 * @param length 数据长度
 * @return CRC16校验码 (16位)
 * @note 使用查表法，效率高；标准Modbus RTU多项式0xA001
 */
uint16_t modbus_crc16(uint8_t *buffer, uint16_t length);

/**
 * @brief 验证Modbus CRC16校验码
 * @param buffer 数据缓冲区 (包含CRC)
 * @param length 数据总长度 (包含2字节CRC)
 * @return 1:校验成功 0:校验失败
 */
uint8_t modbus_check_crc(uint8_t *buffer, uint16_t length);

/**
 * @brief 构建Modbus读寄存器请求帧
 * @param buffer 输出缓冲区 (至少8字节)
 * @param slave_id 从机ID (1-247)
 * @param function_code 功能码 (0x03/0x04)
 * @param start_addr 起始地址
 * @param quantity 寄存器数量
 * @note 请求帧格式: [ID(1)][功能码(1)][起始地址(2)][数量(2)][CRC(2)] = 8字节
 */
void modbus_build_request(uint8_t *buffer, uint8_t slave_id, uint8_t function_code, 
                         uint16_t start_addr, uint16_t quantity);

/**
 * @brief 解析Modbus响应数据
 * @param request 请求数据
 * @param response 响应数据
 * @param response_len 响应数据长度
 * @return 1:成功 0:失败
 * @note 验证从机ID、功能码和CRC
 */
uint8_t modbus_parse_response(uint8_t *request, uint8_t *response, uint16_t response_len);

/**
 * @brief 构建Modbus异常响应帧
 * @param buffer 输出缓冲区 (至少5字节)
 * @param function_code 原始功能码
 * @param exception_code 异常代码 (见MODBUS_EX_*定义)
 * @return 响应帧长度 (5字节)
 * @note 异常响应格式: [ID][功能码|0x80][异常码][CRC]
 */
uint8_t modbus_build_exception_response(uint8_t *buffer, uint8_t function_code, uint8_t exception_code);

/**
 * @brief 构建写单个寄存器请求帧
 * @param buffer 输出缓冲区 (至少8字节)
 * @param slave_id 从机ID (1-247)
 * @param reg_addr 寄存器地址
 * @param value 写入值
 * @return 请求帧长度 (8字节)
 * @note 请求帧格式: [ID][0x06][地址高][地址低][值高][值低][CRC低][CRC高]
 */
uint8_t modbus_build_write_single_reg(uint8_t *buffer, uint8_t slave_id,
                                       uint16_t reg_addr, uint16_t value);

/**
 * @brief 构建写多个寄存器请求帧
 * @param buffer 输出缓冲区 (至少9+2*quantity字节)
 * @param slave_id 从机ID (1-247)
 * @param start_addr 起始地址
 * @param quantity 寄存器数量
 * @param data 要写入的数据数组
 * @return 请求帧长度 (9 + 2*quantity)
 * @note 请求帧格式: [ID][0x10][起始地址高][起始地址低][数量高][数量低][字节数][数据...][CRC低][CRC高]
 */
uint8_t modbus_build_write_multiple_reg(uint8_t *buffer, uint8_t slave_id,
                                         uint16_t start_addr, uint16_t quantity,
                                         const uint16_t *data);

#ifdef __cplusplus
}
#endif

#endif
