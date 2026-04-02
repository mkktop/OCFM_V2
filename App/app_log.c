#include "app_log.h"

/**
 * @brief ��ʼ�����ݼ�¼��
 * 
 * �ú�����ʼ�����ݼ�¼������־����ģ��
 * 
 */
void app_log_data_init(void)
{
    // ��ʼ�����ݼ�¼��
    data_recorder_init(NULL);
    // ��ʼ����־����ģ��
    log_manager_init(NULL);
    // д��ϵͳ��ʼ����־
    log_write(LOG_TYPE_SYSTEM, "SYSTEM_INIT");
    // д���û���¼��־
    log_write(LOG_TYPE_USER, "admin is online");
    // д�뱨��ϵͳ��ʼ����־
    log_write(LOG_TYPE_ALARM, "ALARM_SYSTEM_INIT");

}
