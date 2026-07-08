/**
 * @file at24c02.h
 * @brief M24C02 EEPROM驱动头文件
 * @details M24C02是2K位的串行EEPROM，支持I2C接口
 */

#ifndef __AT24C02_H
#define __AT24C02_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "i2c.h"

/**
 * @brief M24C02设备地址
 * @note A2/A1/A0引脚接地，地址为1010_000 = 0x50
 *       实际地址: 0x50 << 1 = 0xA0 (写) / 0xA1 (读)
 */
#define AT24C02_ADDR         0x50

/**
 * @brief M24C02存储容量
 * @note 2Kbit = 256字节 = 16页 * 16字节
 */
#define AT24C02_SIZE         256

/**
 * @brief 页大小
 * @note M24C02页大小为16字节
 */
#define AT24C02_PAGE_SIZE    16

/**
 * @brief 页数量
 * @note M24C02有16页，每页16字节
 */
#define AT24C02_PAGE_COUNT   16

/**
 * @brief 写入超时时间
 */
#define AT24C02_TIMEOUT      1000

/**
 * @brief 初始化M24C02
 * @return 1:成功 0:失败
 * @note 检查EEPROM是否在线
 */
uint8_t at24c02_init(void);

/**
 * @brief 读取单个字节
 * @param addr 读取地址 (0-255)
 * @param data 数据输出指针
 * @return 1:成功 0:失败
 */
uint8_t at24c02_read_byte(uint8_t addr, uint8_t *data);

/**
 * @brief 写入单个字节
 * @param addr 写入地址 (0-255)
 * @param data 要写入的数据
 * @return 1:成功 0:失败
 * @note 写入时间最长10ms
 */
uint8_t at24c02_write_byte(uint8_t addr, uint8_t data);

/**
 * @brief 连续读取多个字节
 * @param addr 起始地址
 * @param length 读取长度 (1-256)
 * @param data 数据输出缓冲区
 * @return 1:成功 0:失败
 */
uint8_t at24c02_read_buffer(uint8_t addr, uint8_t length, uint8_t *data);

/**
 * @brief 连续写入多个字节
 * @param addr 起始地址
 * @param length 写入长度
 * @param data 要写入的数据
 * @return 1:成功 0:失败
 * @note 自动按页写入，长度超过页剩余空间会自动换页
 */
uint8_t at24c02_write_buffer(uint8_t addr, uint8_t length, uint8_t *data);

/**
 * @brief 写入一页数据
 * @param page 页号 (0-7)
 * @param length 写入长度 (1-32)
 * @param data 要写入的数据
 * @return 1:成功 0:失败
 * @note 页写入地址必须在页内，超出部分会回绕
 */
uint8_t at24c02_write_page(uint8_t page, uint8_t length, uint8_t *data);

/**
 * @brief 擦除整个EEPROM
 * @return 1:成功 0:失败
 * @note 将所有字节写入0xFF
 */
uint8_t at24c02_erase(void);

/**
 * @brief 检查EEPROM是否在线
 * @return 1:在线 0:不在线
 */
uint8_t at24c02_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif
