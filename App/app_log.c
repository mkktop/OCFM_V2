#include "app_log.h"
#include "app_config.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <string.h>

/*============================================================================*/
/*                           异步日志队列                                       */
/*============================================================================*/

/**
 * @brief 日志消息结构 (通过队列传递)
 */
typedef struct {
    uint8_t type;                               /**< 日志类型 */
    char content[LOG_CONTENT_MAX_LEN];          /**< 日志内容 */
} log_msg_t;

#define LOG_QUEUE_SIZE  8

static osMessageQueueId_t g_log_queue = NULL;

/*============================================================================*/
/*                           配置变更回调                                       */
/*============================================================================*/

/**
 * @brief 配置参数名称表 (与 config_id_t 枚举顺序一一对应)
 */
static const char *config_names[CONFIG_ID_COUNT] = {
    "RANGE_MAX", "HEIGHT", "CAL_4MA", "CAL_20MA",
    "RANGE_4MA", "RANGE_20MA", "POINT_NUM",
    "WINDOW_WIDTH", "FILTER_COUNT", "DELAY_TIME", "ANTENNA_TYPE",
    "BLIND_AREA", "W_COEFF", "M_COEFF",
    "MODBUS_ADDR", "MODBUS_BAUD", "MODBUS_STOP",
    "ALARM_AH", "ALARM_AL", "ALARM_DH", "ALARM_DL", "ALARM_AAH", "ALARM_AAL",
    "DIS_OFFSET", "CANALS_TYPE", "CHANNEL_ID", "INSTANT_UNIT", "SUM_POINT",
    "LANGUAGE", "SHOW_ALARM", "FACTORY_RESET", "CLEAR_TOTAL"
};

/**
 * @brief 参数变更日志回调 (仅发送到队列, 不做文件I/O)
 * @param id 变更的配置参数ID
 */
static void on_config_change_log(config_id_t id)
{
    char buf[LOG_CONTENT_MAX_LEN];

    if (id == CONFIG_ID_FACTORY_RESET) {
        app_log_send(LOG_TYPE_USER, "FACTORY_RESET");
        return;
    }
    if (id == CONFIG_ID_CLEAR_TOTAL) {
        app_log_send(LOG_TYPE_USER, "CLEAR_TOTAL_FLOW");
        return;
    }

    if (app_config_is_float(id)) {
        float val = 0;
        app_config_getf(id, &val);
        snprintf(buf, sizeof(buf), "SET %s=%.2f", config_names[id], val);
    } else {
        uint32_t val = 0;
        app_config_get_val(id, &val);
        snprintf(buf, sizeof(buf), "SET %s=%lu", config_names[id], (unsigned long)val);
    }

    app_log_send(LOG_TYPE_USER, buf);
}

/*============================================================================*/
/*                           对外接口                                           */
/*============================================================================*/

/**
 * @brief 发送异步日志 (线程安全)
 * @param type  日志类型
 * @param content 日志内容
 */
void app_log_send(uint8_t type, const char *content)
{
    if (g_log_queue == NULL) return;

    log_msg_t msg;
    msg.type = type;
    strncpy(msg.content, content, LOG_CONTENT_MAX_LEN - 1);
    msg.content[LOG_CONTENT_MAX_LEN - 1] = '\0';

    /* 非阻塞发送, 队列满则丢弃 */
    osMessageQueuePut(g_log_queue, &msg, 0, 0);
}

/**
 * @brief 处理待写入的日志队列 (在log_task中调用)
 */
void app_log_process(void)
{
    if (g_log_queue == NULL) return;

    log_msg_t msg;
    while (osMessageQueueGet(g_log_queue, &msg, NULL, 0) == osOK)
    {
        log_write((LogType)msg.type, msg.content);
    }
}

/**
 * @brief 初始化数据记录和日志模块
 */
void app_log_data_init(void)
{
    /* 创建异步日志队列 */
    g_log_queue = osMessageQueueNew(LOG_QUEUE_SIZE, sizeof(log_msg_t), NULL);

    /* 初始化数据记录器 */
    data_recorder_init(NULL);
    /* 初始化日志管理模块 */
    log_manager_init(NULL);
    /* 写入系统启动日志 (这些在log_task上下文, 直接写即可) */
    log_write(LOG_TYPE_SYSTEM, "SYSTEM_INIT");
    log_write(LOG_TYPE_USER, "admin is online");
    log_write(LOG_TYPE_ALARM, "ALARM_SYSTEM_INIT");
    /* 注册参数变更日志回调 (回调仅发队列, 不做文件I/O) */
    app_config_set_change_callback(on_config_change_log);
}
