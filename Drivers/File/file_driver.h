/**
 * @file file_driver.h
 * @brief FATFS文件系统驱动头文件
 * @details 提供简洁的文件操作API，基于SD卡
 */

#ifndef __FILE_DRIVER_H
#define __FILE_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "ff.h"
#include "fatfs.h"

/**
 * @brief 文件打开模式定义
 */
#define FILE_MODE_READ    0   /**< 只读模式，文件不存在则失败 */
#define FILE_MODE_WRITE   1   /**< 写模式，文件不存在则创建，存在则覆盖 */
#define FILE_MODE_APPEND  2   /**< 追加模式，文件不存在则创建，存在则追加 */
#define FILE_MODE_OPEN    3   /**< 打开模式，文件不存在则失败 */

/**
 * @brief 函数返回状态定义
 */
#define FILE_OK           0   /**< 操作成功 */
#define FILE_ERROR        1   /**< 操作失败 */
#define FILE_NOT_MOUNTED  2   /**< 文件系统未挂载 */
#define FILE_NOT_OPENED   3   /**< 文件未打开 */
#define FILE_READ_ERROR   4   /**< 读取错误 */
#define FILE_WRITE_ERROR  5   /**< 写入错误 */

/**
 * @brief 初始化文件系统
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 挂载SD卡到FATFS文件系统，只需调用一次
 */
uint8_t file_init(void);

/**
 * @brief 打开文件
 * @param filename 文件名（含路径）
 * @param mode 打开模式 FILE_MODE_READ / FILE_MODE_WRITE / FILE_MODE_APPEND / FILE_MODE_OPEN
 * @return FILE_OK:成功 FILE_NOT_MOUNTED:未挂载 FILE_ERROR:打开失败
 */
uint8_t file_open(const char *filename, uint8_t mode);

/**
 * @brief 关闭文件
 * @return FILE_OK:成功
 * @note 写入文件后必须调用此函数确保数据写入SD卡
 */
uint8_t file_close(void);

/**
 * @brief 写入数据到文件
 * @param data 数据缓冲区
 * @param len 要写入的字节数
 * @param written 实际写入的字节数（可为NULL）
 * @return FILE_OK:成功 FILE_NOT_OPENED:文件未打开 FILE_WRITE_ERROR:写入失败
 */
uint8_t file_write(const void *data, uint32_t len, uint32_t *written);

/**
 * @brief 从文件读取数据
 * @param buf 数据缓冲区
 * @param len 要读取的字节数
 * @param read 实际读取的字节数（可为NULL）
 * @return FILE_OK:成功 FILE_NOT_OPENED:文件未打开 FILE_READ_ERROR:读取失败
 */
uint8_t file_read(void *buf, uint32_t len, uint32_t *read);

/**
 * @brief 删除文件
 * @param filename 文件名（含路径）
 * @return FILE_OK:成功 FILE_NOT_MOUNTED:未挂载 FILE_ERROR:删除失败
 */
uint8_t file_delete(const char *filename);

/**
 * @brief 检查文件是否存在
 * @param filename 文件名（含路径）
 * @return 1:存在 0:不存在或未挂载
 */
uint8_t file_exists(const char *filename);

/**
 * @brief 创建目录
 * @param path 目录路径
 * @return FILE_OK:成功 FILE_NOT_MOUNTED:未挂载 FILE_ERROR:创建失败
 */
uint8_t file_create_dir(const char *path);

/**
 * @brief 列出目录内容
 * @param path 目录路径，传入"/"列出根目录
 * @return FILE_OK:成功 FILE_NOT_MOUNTED:未挂载 FILE_ERROR:打开目录失败
 * @note 通过串口打印目录内容
 */
uint8_t file_list_dir(const char *path);

#ifdef __cplusplus
}
#endif

#endif