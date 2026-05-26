/**
 * @file file_driver.c
 * @brief FATFS�ļ�ϵͳ����ʵ��
 * @details ��װFATFS���ò������ṩ�����ļ�����API
 */

#include "file_driver.h"
#include "fatfs_platform.h"

/**
 * @brief �ļ�����
 */
static FIL g_fil;

/**
 * @brief SD�����ر�־
 */
static uint8_t g_mounted = 0;

/**
 * @brief �ļ��򿪱�־
 */
static uint8_t g_opened = 0;

/**
 * @brief 检测SD卡是否在位，不在位时清除挂载和打开标志
 * @return 1:在位 0:不在位
 */
static uint8_t file_check_card(void)
{
    if (BSP_PlatformIsDetected() != SD_PRESENT) {
        g_mounted = 0;
        g_opened = 0;
        return 0;
    }
    return 1;
}

/**
 * @brief ��ʼ���ļ�ϵͳ
 * @return FILE_OK:�ɹ� FILE_ERROR:ʧ��
 * @note ����SD����FATFS�ļ�ϵͳ��ֻ�����һ��
 */
uint8_t file_init(void)
{
    if (!file_check_card()) {
        printf("file_init: SD card not present\r\n");
        return FILE_ERROR;
    }
    // ����Ƿ��ѹ��أ����ѹ���������
    if (g_mounted) {
        printf("file_init: already mounted, skip\r\n");
        return FILE_OK;
    }
    // ����SD����FATFS�ļ�ϵͳ 
    printf("file_init: starting f_mount...\r\n");
    FRESULT res = f_mount(&SDFatFS, SDPath, 1);
    printf("file_init: f_mount result = %d\r\n", res);
    // �����ؽ��
    if (res == FR_OK) {
        g_mounted = 1;
        
        /* ��� SD ��״̬ */
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
 * @brief ���ļ�
 * @param filename �ļ�������·����
 * @param mode ��ģʽ FILE_MODE_READ / FILE_MODE_WRITE / FILE_MODE_APPEND / FILE_MODE_OPEN
 * @return FILE_OK:�ɹ� FILE_NOT_MOUNTED:δ���� FILE_ERROR:��ʧ��
 */
uint8_t file_open(const char *filename, uint8_t mode)
{
    if (!file_check_card()) {
        return FILE_NOT_MOUNTED;
    }

    if (!g_mounted) {
        if (file_init() != FILE_OK) {
            return FILE_NOT_MOUNTED;
        }
    }

    if (g_opened) {
        printf("file_open: WARNING already open, closing first\r\n");
        f_close(&g_fil);
        g_opened = 0;
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

    printf("file_open: f_open %s mode=%u\r\n", filename, fatfs_mode);
    FRESULT res = f_open(&g_fil, filename, fatfs_mode);
    printf("file_open: result = %d\r\n", res);
    if (res == FR_OK) {
        g_opened = 1;
        return FILE_OK;
    }
    return FILE_ERROR;
}

/**
 * @brief �ر��ļ�
 * @return FILE_OK:�ɹ�
 * @note д���ļ��������ô˺���ȷ������д��SD��
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
 * @brief д�����ݵ��ļ�
 * @param data ���ݻ�����
 * @param len Ҫд����ֽ���
 * @param written ʵ��д����ֽ�������ΪNULL��
 * @return FILE_OK:�ɹ� FILE_NOT_OPENED:�ļ�δ�� FILE_WRITE_ERROR:д��ʧ��
 */
uint8_t file_write(const void *data, uint32_t len, uint32_t *written)
{
    if (!file_check_card()) {
        return FILE_NOT_OPENED;
    }

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
 * @brief ���ļ���ȡ����
 * @param buf ���ݻ�����
 * @param len Ҫ��ȡ���ֽ���
 * @param read ʵ�ʶ�ȡ���ֽ�������ΪNULL��
 * @return FILE_OK:�ɹ� FILE_NOT_OPENED:�ļ�δ�� FILE_READ_ERROR:��ȡʧ��
 */
uint8_t file_read(void *buf, uint32_t len, uint32_t *read)
{
    if (!file_check_card()) {
        return FILE_NOT_OPENED;
    }

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
 * @brief ɾ���ļ�
 * @param filename �ļ�������·����
 * @return FILE_OK:�ɹ� FILE_NOT_MOUNTED:δ���� FILE_ERROR:ɾ��ʧ��
 */
uint8_t file_delete(const char *filename)
{
    if (!file_check_card()) {
        return FILE_NOT_MOUNTED;
    }

    if (!g_mounted) {
        if (file_init() != FILE_OK) {
            return FILE_NOT_MOUNTED;
        }
    }

    FRESULT res = f_unlink(filename);
    return (res == FR_OK) ? FILE_OK : FILE_ERROR;
}

/**
 * @brief ����ļ��Ƿ����
 * @param filename �ļ�������·����
 * @return 1:���� 0:�����ڻ�δ����
 */
uint8_t file_exists(const char *filename)
{
    if (!file_check_card()) {
        return 0;
    }

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
 * @brief ����Ŀ¼
 * @param path Ŀ¼·��
 * @return FILE_OK:�ɹ� FILE_NOT_MOUNTED:δ���� FILE_ERROR:����ʧ��
 */
uint8_t file_create_dir(const char *path)
{
    if (!file_check_card()) {
        return FILE_NOT_MOUNTED;
    }

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
 * @brief �г�Ŀ¼����
 * @param path Ŀ¼·��������"/"�г���Ŀ¼
 * @return FILE_OK:�ɹ� FILE_NOT_MOUNTED:δ���� FILE_ERROR:��Ŀ¼ʧ��
 * @note ͨ�����ڴ�ӡĿ¼����
 */
uint8_t file_list_dir(const char *path)
{
    if (!file_check_card()) {
        return FILE_NOT_MOUNTED;
    }

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