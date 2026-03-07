/**
 * @file at24c02.c
 * @brief AT24C02 EEPROM驱动实现
 * @details AT24C02是2K位的串行EEPROM，支持I2C接口
 */

#include "at24c02.h"
#include "i2c.h"
#include <string.h>

/**
 * @brief 初始化AT24C02
 * @return 1:成功 0:失败
 * @note 检查EEPROM是否在线
 */
uint8_t at24c02_init(void)
{
    return at24c02_is_ready();
}

/**
 * @brief 读取单个字节
 * @param addr 读取地址 (0-255)
 * @param data 数据输出指针
 * @return 1:成功 0:失败
 */
uint8_t at24c02_read_byte(uint8_t addr, uint8_t *data)
{
    if (data == NULL || addr >= AT24C02_SIZE)
    {
        return 0;
    }
    
    return at24c02_read_buffer(addr, 1, data);
}

/**
 * @brief 写入单个字节
 * @param addr 写入地址 (0-255)
 * @param data 要写入的数据
 * @return 1:成功 0:失败
 * @note 写入时间最长10ms
 */
uint8_t at24c02_write_byte(uint8_t addr, uint8_t data)
{
    if (addr >= AT24C02_SIZE)
    {
        return 0;
    }
    
    return at24c02_write_buffer(addr, 1, &data);
}

/**
 * @brief 连续读取多个字节
 * @param addr 起始地址
 * @param length 读取长度 (1-256)
 * @param data 数据输出缓冲区
 * @return 1:成功 0:失败
 */
uint8_t at24c02_read_buffer(uint8_t addr, uint8_t length, uint8_t *data)
{
    HAL_StatusTypeDef status;
    
    if (data == NULL || addr + length > AT24C02_SIZE)
    {
        return 0;
    }
    
    /* 发送设备地址+写操作 */
    status = HAL_I2C_Master_Transmit(&hi2c2, 
                                      AT24C02_ADDR << 1, 
                                      &addr, 1, 
                                      AT24C02_TIMEOUT);
    if (status != HAL_OK)
    {
        return 0;
    }
    
    /* 发送设备地址+读操作 */
    status = HAL_I2C_Master_Receive(&hi2c2, 
                                     (AT24C02_ADDR << 1) | 0x01, 
                                     data, length, 
                                     AT24C02_TIMEOUT);
    if (status != HAL_OK)
    {
        return 0;
    }
    
    return 1;
}

/**
 * @brief 连续写入多个字节
 * @param addr 起始地址
 * @param length 写入长度
 * @param data 要写入的数据
 * @return 1:成功 0:失败
 * @note 自动按页写入，长度超过页剩余空间会自动换页
 */
uint8_t at24c02_write_buffer(uint8_t addr, uint8_t length, uint8_t *data)
{
    HAL_StatusTypeDef status;
    uint8_t page_remain;
    uint8_t write_len;
    uint8_t current_addr = addr;
    uint8_t *current_data = data;
    
    if (data == NULL || addr + length > AT24C02_SIZE)
    {
        return 0;
    }
    
    while (length > 0)
    {
        /* 计算当前页剩余空间 */
        page_remain = AT24C02_PAGE_SIZE - (current_addr % AT24C02_PAGE_SIZE);
        
        /* 本次写入长度不能超过页剩余空间 */
        write_len = (length < page_remain) ? length : page_remain;
        
        /* 发送设备地址+写操作 */
        uint8_t tx_buffer[33];  /* 地址 + 最多32字节数据 */
        tx_buffer[0] = current_addr;
        memcpy(&tx_buffer[1], current_data, write_len);
        
        status = HAL_I2C_Master_Transmit(&hi2c2, 
                                          AT24C02_ADDR << 1, 
                                          tx_buffer, 
                                          write_len + 1, 
                                          AT24C02_TIMEOUT);
        if (status != HAL_OK)
        {
            return 0;
        }
        
        /* 等待内部写完成 (最大10ms) */
        HAL_Delay(10);
        
        current_addr += write_len;
        current_data += write_len;
        length -= write_len;
    }
    
    return 1;
}

/**
 * @brief 写入一页数据
 * @param page 页号 (0-7)
 * @param length 写入长度 (1-32)
 * @param data 要写入的数据
 * @return 1:成功 0:失败
 * @note 页写入地址必须在页内，超出部分会回绕
 */
uint8_t at24c02_write_page(uint8_t page, uint8_t length, uint8_t *data)
{
    HAL_StatusTypeDef status;
    uint8_t tx_buffer[33];  /* 地址 + 最多32字节数据 */
    
    if (data == NULL || page >= AT24C02_PAGE_COUNT || length > AT24C02_PAGE_SIZE)
    {
        return 0;
    }
    
    /* 计算页起始地址 */
    tx_buffer[0] = page * AT24C02_PAGE_SIZE;
    
    /* 复制数据 */
    memcpy(&tx_buffer[1], data, length);
    
    /* 发送数据 */
    status = HAL_I2C_Master_Transmit(&hi2c2, 
                                      AT24C02_ADDR << 1, 
                                      tx_buffer, 
                                      length + 1, 
                                      AT24C02_TIMEOUT);
    if (status != HAL_OK)
    {
        return 0;
    }
    
    /* 等待内部写完成 */
    HAL_Delay(10);
    
    return 1;
}

/**
 * @brief 擦除整个EEPROM
 * @return 1:成功 0:失败
 * @note 将所有字节写入0xFF
 */
uint8_t at24c02_erase(void)
{
    uint8_t buffer[AT24C02_PAGE_SIZE];
    uint8_t page;
    
    /* 填充0xFF */
    memset(buffer, 0xFF, AT24C02_PAGE_SIZE);
    
    /* 按页擦除 */
    for (page = 0; page < AT24C02_PAGE_COUNT; page++)
    {
        if (!at24c02_write_page(page, AT24C02_PAGE_SIZE, buffer))
        {
            return 0;
        }
    }
    
    return 1;
}

/**
 * @brief 检查EEPROM是否在线
 * @return 1:在线 0:不在线
 */
uint8_t at24c02_is_ready(void)
{
    return (HAL_I2C_IsDeviceReady(&hi2c2, 
                                   AT24C02_ADDR << 1, 
                                   3, 
                                   AT24C02_TIMEOUT) == HAL_OK) ? 1 : 0;
}