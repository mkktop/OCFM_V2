#ifndef __APP_LOG_H__
#define __APP_LOG_H__
#include "global.h"

void app_log_data_init(void);

/**
 * @brief 发送异步日志 (线程安全, 可从任意任务/定时器上下文调用)
 * @param type  日志类型 (LOG_TYPE_SYSTEM / LOG_TYPE_USER / LOG_TYPE_ALARM)
 * @param content 日志内容 (会被拷贝, 调用者无需保持)
 */
void app_log_send(uint8_t type, const char *content);

/**
 * @brief 处理待写入的日志队列 (在log_task中周期调用)
 */
void app_log_process(void);

#endif /* __APP_LOG_H__ */
