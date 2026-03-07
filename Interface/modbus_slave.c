/**
 * @file modbus_slave.c
 * @brief Modbus从机实现 - 响应用户指令
 * @details 通过串口2连接用户设备，响应Modbus指令
 */

#include "modbus_slave.h"
#include "modbus.h"
#include <string.h>

/**
 * @brief 保持寄存器数组
 * @note 所有数据都使用保持寄存器存储，地址范围: 0x0000 - 0x003F
 */
static uint16_t holding_registers[HOLDING_REG_SIZE];

/**
 * @brief 初始化Modbus从机
 * @param slave 从机结构体指针
 * @param huart 串口句柄指针
 * @note 初始化串口和寄存器数据
 */
void modbus_slave_init(modbus_slave_t *slave, UART_HandleTypeDef *huart)
{
    /* 清零结构体 */
    memset(slave, 0, sizeof(modbus_slave_t));
    
    /* 设置串口句柄 */
    slave->huart = huart;
    
    /* 设置初始状态 */
    slave->state = MODBUS_SLAVE_STATE_IDLE;
    
    /* 设置默认从机ID */
    slave->slave_id = MODBUS_SLAVE_ID;
    
    /* 清零寄存器 */
    memset(holding_registers, 0, sizeof(holding_registers));
}

/**
 * @brief 设置从机ID
 * @param slave 从机结构体指针
 * @param id 从机ID (1-247)
 */
void modbus_slave_set_id(modbus_slave_t *slave, uint8_t id)
{
    if (id > 0 && id <= 247)
    {
        slave->slave_id = id;
    }
}

/**
 * @brief Modbus从机任务处理
 * @param slave 从机结构体指针
 * @note 在主循环中调用，处理接收到的数据并响应
 */
void modbus_slave_task(modbus_slave_t *slave)
{
    uint8_t ch;
    
    /* 尝试接收单字节数据 */
    if (HAL_UART_Receive(slave->huart, &ch, 1, 1) == HAL_OK)
    {
        /* 将数据存入接收缓冲区 */
        slave->rx_buffer[slave->rx_length++] = ch;
        slave->last_receive_time = HAL_GetTick();
        
        /* 检查缓冲区是否溢出 */
        if (slave->rx_length >= MODBUS_SLAVE_BUF_SIZE)
        {
            slave->rx_length = 0;
        }
    }
    else
    {
        /* 检查是否接收完成 (3.5个字符时间无新数据) */
        if (slave->rx_length > 0 && 
            (HAL_GetTick() - slave->last_receive_time) > 10)
        {
            /* 处理接收到的数据 */
            uint16_t response_len = modbus_slave_process(slave, 
                                                          slave->rx_buffer, 
                                                          slave->rx_length);
            
            /* 如果有响应数据，发送响应 */
            if (response_len > 0)
            {
                modbus_slave_send(slave, slave->tx_buffer, response_len);
            }
            
            /* 清空接收缓冲区 */
            slave->rx_length = 0;
        }
    }
}

/**
 * @brief 处理接收到的数据
 * @param slave 从机结构体指针
 * @param data 接收到的数据
 * @param length 数据长度
 * @return 处理后的响应长度，0表示无需响应
 */
uint16_t modbus_slave_process(modbus_slave_t *slave, uint8_t *data, uint16_t length)
{
    /* 检查最小帧长度 (地址+功能码+CRC = 4字节) */
    if (length < 4)
    {
        return 0;
    }
    
    /* 验证CRC */
    if (!modbus_check_crc(data, length))
    {
        return 0;
    }
    
    /* 检查从机地址是否匹配 */
    if (data[0] != slave->slave_id && data[0] != 0)
    {
        return 0;
    }
    
    /* 获取功能码 */
    uint8_t function_code = data[1];
    
    /* 根据功能码处理请求 */
    switch (function_code)
    {
        case MODBUS_FUNC_READ_HOLDING:
        {
            /* 读保持寄存器 (0x03) */
            uint16_t start_addr = (data[2] << 8) | data[3];
            uint16_t quantity = (data[4] << 8) | data[5];
            return modbus_slave_read_holding_registers(slave, start_addr, quantity, slave->tx_buffer);
        }
        
        case MODBUS_FUNC_WRITE_SINGLE_REG:
        {
            /* 写单个寄存器 (0x06) */
            uint16_t register_addr = (data[2] << 8) | data[3];
            uint16_t value = (data[4] << 8) | data[5];
            return modbus_slave_write_single_register(slave, register_addr, value, slave->tx_buffer);
        }
        
        case MODBUS_FUNC_WRITE_MULTIPLE_REG:
        {
            /* 写多个寄存器 (0x10) */
            uint16_t start_addr = (data[2] << 8) | data[3];
            uint16_t quantity = (data[4] << 8) | data[5];
            uint8_t byte_count = data[6];
            return modbus_slave_write_multiple_registers(slave, start_addr, quantity, &data[7], slave->tx_buffer);
        }
        
        default:
        {
            /* 不支持的功能码，返回异常响应 */
            return modbus_slave_build_exception(slave, function_code, 
                                                 MODBUS_EX_ILLEGAL_FUNCTION, 
                                                 slave->tx_buffer);
        }
    }
}

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
                                               uint8_t *response)
{
    /* 检查地址范围 */
    if (start_addr + quantity > HOLDING_REG_SIZE)
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_READ_HOLDING, 
                                             MODBUS_EX_ILLEGAL_DATA_ADDR, response);
    }
    
    /* 检查数量范围 (最大125个寄存器) */
    if (quantity < 1 || quantity > 125)
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_READ_HOLDING, 
                                             MODBUS_EX_ILLEGAL_DATA_VALUE, response);
    }
    
    /* 构建响应帧 */
    response[0] = slave->slave_id;              /* 从机地址 */
    response[1] = MODBUS_FUNC_READ_HOLDING;     /* 功能码 */
    response[2] = quantity * 2;                 /* 字节数 */
    
    /* 填充寄存器数据 (大端格式) */
    for (uint16_t i = 0; i < quantity; i++)
    {
        response[3 + i * 2] = holding_registers[start_addr + i] >> 8;       /* 高字节 */
        response[3 + i * 2 + 1] = holding_registers[start_addr + i] & 0xFF; /* 低字节 */
    }
    
    /* 计算CRC */
    uint16_t crc = modbus_crc16(response, 3 + quantity * 2);
    response[3 + quantity * 2] = crc & 0xFF;        /* CRC低字节 */
    response[3 + quantity * 2 + 1] = crc >> 8;      /* CRC高字节 */
    
    /* 返回响应长度 */
    return 5 + quantity * 2;
}

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
                                              uint8_t *response)
{
    /* 检查地址范围 */
    if (register_addr >= HOLDING_REG_SIZE)
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_WRITE_SINGLE_REG, 
                                             MODBUS_EX_ILLEGAL_DATA_ADDR, response);
    }
    
    /* 写入寄存器 */
    holding_registers[register_addr] = value;
    
    /* 构建响应帧 (原样返回请求) */
    response[0] = slave->slave_id;
    response[1] = MODBUS_FUNC_WRITE_SINGLE_REG;
    response[2] = register_addr >> 8;
    response[3] = register_addr & 0xFF;
    response[4] = value >> 8;
    response[5] = value & 0xFF;
    
    /* 计算CRC */
    uint16_t crc = modbus_crc16(response, 6);
    response[6] = crc & 0xFF;
    response[7] = crc >> 8;
    
    return 8;
}

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
                                                 uint8_t *response)
{
    /* 检查地址范围 */
    if (start_addr + quantity > HOLDING_REG_SIZE)
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_WRITE_MULTIPLE_REG, 
                                             MODBUS_EX_ILLEGAL_DATA_ADDR, response);
    }
    
    /* 检查数量范围 */
    if (quantity < 1 || quantity > 123)
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_WRITE_MULTIPLE_REG, 
                                             MODBUS_EX_ILLEGAL_DATA_VALUE, response);
    }
    
    /* 写入寄存器 */
    for (uint16_t i = 0; i < quantity; i++)
    {
        holding_registers[start_addr + i] = (data[i * 2] << 8) | data[i * 2 + 1];
    }
    
    /* 构建响应帧 */
    response[0] = slave->slave_id;
    response[1] = MODBUS_FUNC_WRITE_MULTIPLE_REG;
    response[2] = start_addr >> 8;
    response[3] = start_addr & 0xFF;
    response[4] = quantity >> 8;
    response[5] = quantity & 0xFF;
    
    /* 计算CRC */
    uint16_t crc = modbus_crc16(response, 6);
    response[6] = crc & 0xFF;
    response[7] = crc >> 8;
    
    return 8;
}

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
                                        uint8_t *response)
{
    /* 构建异常响应帧 */
    response[0] = slave->slave_id;
    response[1] = function_code | 0x80;     /* 功能码最高位置1 */
    response[2] = exception_code;
    
    /* 计算CRC */
    uint16_t crc = modbus_crc16(response, 3);
    response[3] = crc & 0xFF;
    response[4] = crc >> 8;
    
    return 5;
}

/**
 * @brief 发送响应数据
 * @param slave 从机结构体指针
 * @param data 数据
 * @param length 长度
 */
void modbus_slave_send(modbus_slave_t *slave, uint8_t *data, uint16_t length)
{
    HAL_UART_Transmit(slave->huart, data, length, 100);
}

/**
 * @brief 获取保持寄存器值
 * @param addr 寄存器地址
 * @return 寄存器值
 */
uint16_t modbus_slave_get_holding_register(uint16_t addr)
{
    if (addr < HOLDING_REG_SIZE)
    {
        return holding_registers[addr];
    }
    return 0;
}

/**
 * @brief 设置保持寄存器值
 * @param addr 寄存器地址
 * @param value 值
 */
void modbus_slave_set_holding_register(uint16_t addr, uint16_t value)
{
    if (addr < HOLDING_REG_SIZE)
    {
        holding_registers[addr] = value;
    }
}

/**
 * @brief 设置32位无符号整数 (占用2个寄存器)
 * @param addr 起始寄存器地址
 * @param value 32位值
 * @note 大端格式: 高位寄存器在前
 *       寄存器addr存储高16位，寄存器addr+1存储低16位
 */
void modbus_slave_set_uint32(uint16_t addr, uint32_t value)
{
    if (addr + 1 < HOLDING_REG_SIZE)
    {
        holding_registers[addr] = (uint16_t)(value >> 16);      /* 高16位 */
        holding_registers[addr + 1] = (uint16_t)(value & 0xFFFF); /* 低16位 */
    }
}

/**
 * @brief 获取32位无符号整数
 * @param addr 起始寄存器地址
 * @return 32位值
 */
uint32_t modbus_slave_get_uint32(uint16_t addr)
{
    if (addr + 1 < HOLDING_REG_SIZE)
    {
        return ((uint32_t)holding_registers[addr] << 16) | 
               (uint32_t)holding_registers[addr + 1];
    }
    return 0;
}

/**
 * @brief 设置32位有符号整数 (占用2个寄存器)
 * @param addr 起始寄存器地址
 * @param value 32位值
 */
void modbus_slave_set_int32(uint16_t addr, int32_t value)
{
    modbus_slave_set_uint32(addr, (uint32_t)value);
}

/**
 * @brief 获取32位有符号整数
 * @param addr 起始寄存器地址
 * @return 32位值
 */
int32_t modbus_slave_get_int32(uint16_t addr)
{
    return (int32_t)modbus_slave_get_uint32(addr);
}

/**
 * @brief 设置浮点数 (占用2个寄存器)
 * @param addr 起始寄存器地址
 * @param value 浮点数值
 * @note IEEE 754格式存储，大端模式
 *       通过联合体实现float与uint32的转换
 */
void modbus_slave_set_float(uint16_t addr, float value)
{
    /* 使用联合体实现float到uint32的转换 */
    typedef union {
        float f;
        uint32_t u;
    } float_union_t;
    
    float_union_t fu;
    fu.f = value;
    
    modbus_slave_set_uint32(addr, fu.u);
}

/**
 * @brief 获取浮点数
 * @param addr 起始寄存器地址
 * @return 浮点数值
 */
float modbus_slave_get_float(uint16_t addr)
{
    typedef union {
        float f;
        uint32_t u;
    } float_union_t;
    
    float_union_t fu;
    fu.u = modbus_slave_get_uint32(addr);
    
    return fu.f;
}

/**
 * @brief 设置双精度浮点数 (占用4个寄存器)
 * @param addr 起始寄存器地址
 * @param value 双精度浮点数值
 * @note IEEE 754格式存储，大端模式
 *       寄存器addr存储最高16位，addr+3存储最低16位
 */
void modbus_slave_set_double(uint16_t addr, double value)
{
    if (addr + 3 < HOLDING_REG_SIZE)
    {
        /* 使用联合体实现double到uint64的转换 */
        typedef union {
            double d;
            uint64_t u;
        } double_union_t;
        
        double_union_t du;
        du.d = value;
        
        /* 分解为4个16位寄存器存储 (大端模式) */
        holding_registers[addr] = (uint16_t)(du.u >> 48);
        holding_registers[addr + 1] = (uint16_t)(du.u >> 32);
        holding_registers[addr + 2] = (uint16_t)(du.u >> 16);
        holding_registers[addr + 3] = (uint16_t)(du.u & 0xFFFF);
    }
}

/**
 * @brief 获取双精度浮点数
 * @param addr 起始寄存器地址
 * @return 双精度浮点数值
 */
double modbus_slave_get_double(uint16_t addr)
{
    if (addr + 3 < HOLDING_REG_SIZE)
    {
        typedef union {
            double d;
            uint64_t u;
        } double_union_t;
        
        double_union_t du;
        du.u = ((uint64_t)holding_registers[addr] << 48) |
               ((uint64_t)holding_registers[addr + 1] << 32) |
               ((uint64_t)holding_registers[addr + 2] << 16) |
               ((uint64_t)holding_registers[addr + 3]);
        
        return du.d;
    }
    return 0.0;
}