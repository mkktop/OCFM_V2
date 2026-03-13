/**
 * @file file_driver.c
 * @brief FATFS文件系统驱动实现
 * @details 封装FATFS常用操作，提供简洁的文件访问API
 */

#include "file_driver.h"

/**
 * @brief 文件对象
 */
static FIL g_fil;

/**
 * @brief SD卡挂载标志
 */
static uint8_t g_mounted = 0;

/**
 * @brief 文件打开标志
 */
static uint8_t g_opened = 0;

/**
 * @brief 初始化文件系统
 * @return FILE_OK:成功 FILE_ERROR:失败
 * @note 挂载SD卡到FATFS文件系统，只需调用一次
 */
uint8_t file_init(void)
{
    // 检查是否已挂载，若已挂载则跳过
    if (g_mounted) {
        printf("file_init: already mounted, skip\r\n");
        return FILE_OK;
    }
    // 挂载SD卡到FATFS文件系统 
    printf("file_init: starting f_mount...\r\n");
    FRESULT res = f_mount(&SDFatFS, SDPath, 1);
    printf("file_init: f_mount result = %d\r\n", res);
    // 检查挂载结果
    if (res == FR_OK) {
        g_mounted = 1;
        
        /* 检查 SD 卡状态 */
        printf("file_init: checking SD card status...\r\n");
        DWORD free_clusters, free_sectors, total_sectors;
        FATFS *fs;
        res = f_getfree("0:", &free_clusters, &fs);
        printf("file_init: f_getfree result = %d\r\n", res);
        
        if (res == FR_OK) {
            total_sectors = (fs->n_fatent - 2) * fs->csize;
            free_sectors = free_clusters * fs->csize;
            printf("file_init: SD OK! Free: %lu sectors\r\n", free_sectors);
        }
        
        printf("file_init: mount OK\r\n");
        return FILE_OK;
    }
    
    printf("file_init: mount FAILED, error code: %d\r\n", res);
    return FILE_ERROR;
}

/**
 * @brief 打开文件
 * @param filename 文件名（含路径）
 * @param mode 打开模式 FILE_MODE_READ / FILE_MODE_WRITE / FILE_MODE_APPEND / FILE_MODE_OPEN
 * @return FILE_OK:成功 FILE_NOT_MOUNTED:未挂载 FILE_ERROR:打开失败
 */
uint8_t file_open(const char *filename, uint8_t mode)
{
    if (!g_mounted) {
        if (file_init() != FILE_OK) {
            return FILE_NOT_MOUNTED;
        }
    }

    BYTE fatfs_mode = 0;
    if (mode == FILE_MODE_READ) {
        fatfs_mode = FA_READ;
    } else if (mode == FILE_MODE_WRITE) {
        fatfs_mode = FA_CREATE_ALWAYS | FA_WRITE;
    } else if (mode == FILE_MODE_APPEND) {
        fatfs_mode = FA_OPEN_APPEND | FA_WRITE;
    } else {
        fatfs_mode = FA_OPEN_EXISTING | FA_READ;
    }

    FRESULT res = f_open(&g_fil, filename, fatfs_mode);
    if (res == FR_OK) {
        g_opened = 1;
        return FILE_OK;
    }
    return FILE_ERROR;
}

/**
 * @brief 关闭文件
 * @return FILE_OK:成功
 * @note 写入文件后必须调用此函数确保数据写入SD卡
 */
uint8_t file_close(void)
{
    if (g_opened) {
        f_close(&g_fil);
        g_opened = 0;
    }
    return FILE_OK;
}

/**
 * @brief 写入数据到文件
 * @param data 数据缓冲区
 * @param len 要写入的字节数
 * @param written 实际写入的字节数（可为NULL）
 * @return FILE_OK:成功 FILE_NOT_OPENED:文件未打开 FILE_WRITE_ERROR:写入失败
 */
uint8_t file_write(const void *data, uint32_t len, uint32_t *written)
{
    if (!g_opened) {
        return FILE_NOT_OPENED;
    }

    UINT bw = 0;
    FRESULT res = f_write(&g_fil, data, len, &bw);
    if (written) {
        *written = bw;
    }
    
    if (res == FR_OK) {
        f_sync(&g_fil);
        printf("file_write: f_sync done\r\n");
        return FILE_OK;
    }
    
    printf("file_write: FAILED with FatFs error = %d\r\n", res);
    return FILE_WRITE_ERROR;
}

/**
 * @brief 从文件读取数据
 * @param buf 数据缓冲区
 * @param len 要读取的字节数
 * @param read 实际读取的字节数（可为NULL）
 * @return FILE_OK:成功 FILE_NOT_OPENED:文件未打开 FILE_READ_ERROR:读取失败
 */
uint8_t file_read(void *buf, uint32_t len, uint32_t *read)
{
    if (!g_opened) {
        return FILE_NOT_OPENED;
    }

    UINT br = 0;
    FRESULT res = f_read(&g_fil, buf, len, &br);
    if (read) {
        *read = br;
    }

    return (res == FR_OK) ? FILE_OK : FILE_READ_ERROR;
}

/**
 * @brief 删除文件
 * @param filename 文件名（含路径）
 * @return FILE_OK:成功 FILE_NOT_MOUNTED:未挂载 FILE_ERROR:删除失败
 */
uint8_t file_delete(const char *filename)
{
    if (!g_mounted) {
        if (file_init() != FILE_OK) {
            return FILE_NOT_MOUNTED;
        }
    }

    FRESULT res = f_unlink(filename);
    return (res == FR_OK) ? FILE_OK : FILE_ERROR;
}

/**
 * @brief 检查文件是否存在
 * @param filename 文件名（含路径）
 * @return 1:存在 0:不存在或未挂载
 */
uint8_t file_exists(const char *filename)
{
    if (!g_mounted) {
        printf("file_exists: not mounted, calling init...\r\n");
        if (file_init() != FILE_OK) {
            return 0;
        }
    }

    printf("file_exists: checking %s\r\n", filename);
    FILINFO fno;
    uint8_t result = (f_stat(filename, &fno) == FR_OK) ? 1 : 0;
    printf("file_exists: result = %d\r\n", result);
    return result;
}

/**
 * @brief 创建目录
 * @param path 目录路径
 * @return FILE_OK:成功 FILE_NOT_MOUNTED:未挂载 FILE_ERROR:创建失败
 */
uint8_t file_create_dir(const char *path)
{
    if (!g_mounted) {
        printf("file_create_dir: not mounted, calling init...\r\n");
        if (file_init() != FILE_OK) {
            return FILE_NOT_MOUNTED;
        }
    }

    printf("file_create_dir: creating %s\r\n", path);
    printf("file_create_dir: about to call f_mkdir...\r\n");
    FRESULT res = f_mkdir(path);
    printf("file_create_dir: f_mkdir result = %d\r\n", res);
    
    if (res != FR_OK) {
        printf("file_create_dir: FAILED, error code: %d\r\n", res);
    }
    
    return (res == FR_OK) ? FILE_OK : FILE_ERROR;
}

/**
 * @brief 列出目录内容
 * @param path 目录路径，传入"/"列出根目录
 * @return FILE_OK:成功 FILE_NOT_MOUNTED:未挂载 FILE_ERROR:打开目录失败
 * @note 通过串口打印目录内容
 */
uint8_t file_list_dir(const char *path)
{
    if (!g_mounted) {
        if (file_init() != FILE_OK) {
            return FILE_NOT_MOUNTED;
        }
    }

    DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, path);
    if (res != FR_OK) {
        return FILE_ERROR;
    }

    printf("=== Directory: %s ===\r\n", path);
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) {
            break;
        }
        if (fno.fattrib & AM_DIR) {
            printf("[DIR]  %s\r\n", fno.fname);
        } else {
            printf("[FILE] %s (%lu bytes)\r\n", fno.fname, fno.fsize);
        }
    }
    f_closedir(&dir);

    return FILE_OK;
}