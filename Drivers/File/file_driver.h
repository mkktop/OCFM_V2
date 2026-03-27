/**
 * @file file_driver.h
 * @brief FATFS�ļ�ϵͳ����ͷ�ļ�
 * @details �ṩ�����ļ�����API������SD��
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
 * @brief �ļ���ģʽ����
 */
#define FILE_MODE_READ    0   /**< ֻ��ģʽ���ļ���������ʧ�� */
#define FILE_MODE_WRITE   1   /**< дģʽ���ļ��������򴴽��������򸲸� */
#define FILE_MODE_APPEND  2   /**< ׷��ģʽ���ļ��������򴴽���������׷�� */
#define FILE_MODE_OPEN    3   /**< ��ģʽ���ļ���������ʧ�� */

/**
 * @brief ��������״̬����
 */
#define FILE_OK           0   /**< �����ɹ� */
#define FILE_ERROR        1   /**< ����ʧ�� */
#define FILE_NOT_MOUNTED  2   /**< �ļ�ϵͳδ���� */
#define FILE_NOT_OPENED   3   /**< �ļ�δ�� */
#define FILE_READ_ERROR   4   /**< ��ȡ���� */
#define FILE_WRITE_ERROR  5   /**< д����� */

/**
 * @brief ��ʼ���ļ�ϵͳ
 * @return FILE_OK:�ɹ� FILE_ERROR:ʧ��
 * @note ����SD����FATFS�ļ�ϵͳ��ֻ�����һ��
 */
uint8_t file_init(void);

/**
 * @brief ���ļ�
 * @param filename �ļ�������·����
 * @param mode ��ģʽ FILE_MODE_READ / FILE_MODE_WRITE / FILE_MODE_APPEND / FILE_MODE_OPEN
 * @return FILE_OK:�ɹ� FILE_NOT_MOUNTED:δ���� FILE_ERROR:��ʧ��
 */
uint8_t file_open(const char *filename, uint8_t mode);

/**
 * @brief �ر��ļ�
 * @return FILE_OK:�ɹ�
 * @note д���ļ��������ô˺���ȷ������д��SD��
 */
uint8_t file_close(void);

/**
 * @brief д�����ݵ��ļ�
 * @param data ���ݻ�����
 * @param len Ҫд����ֽ���
 * @param written ʵ��д����ֽ�������ΪNULL��
 * @return FILE_OK:�ɹ� FILE_NOT_OPENED:�ļ�δ�� FILE_WRITE_ERROR:д��ʧ��
 */
uint8_t file_write(const void *data, uint32_t len, uint32_t *written);

/**
 * @brief ���ļ���ȡ����
 * @param buf ���ݻ�����
 * @param len Ҫ��ȡ���ֽ���
 * @param read ʵ�ʶ�ȡ���ֽ�������ΪNULL��
 * @return FILE_OK:�ɹ� FILE_NOT_OPENED:�ļ�δ�� FILE_READ_ERROR:��ȡʧ��
 */
uint8_t file_read(void *buf, uint32_t len, uint32_t *read);

/**
 * @brief ɾ���ļ�
 * @param filename �ļ�������·����
 * @return FILE_OK:�ɹ� FILE_NOT_MOUNTED:δ���� FILE_ERROR:ɾ��ʧ��
 */
uint8_t file_delete(const char *filename);

/**
 * @brief ����ļ��Ƿ����
 * @param filename �ļ�������·����
 * @return 1:���� 0:�����ڻ�δ����
 */
uint8_t file_exists(const char *filename);

/**
 * @brief ����Ŀ¼
 * @param path Ŀ¼·��
 * @return FILE_OK:�ɹ� FILE_NOT_MOUNTED:δ���� FILE_ERROR:����ʧ��
 */
uint8_t file_create_dir(const char *path);

/**
 * @brief �г�Ŀ¼����
 * @param path Ŀ¼·��������"/"�г���Ŀ¼
 * @return FILE_OK:�ɹ� FILE_NOT_MOUNTED:δ���� FILE_ERROR:��Ŀ¼ʧ��
 * @note ͨ�����ڴ�ӡĿ¼����
 */
uint8_t file_list_dir(const char *path);

#ifdef __cplusplus
}
#endif

#endif
