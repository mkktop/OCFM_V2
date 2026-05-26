#include "app_log.h"
#include "app_config.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <string.h>
#include "iwdg.h"
#include "ui/ui_history.h"

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

/** @brief 清除SD卡数据请求标志 */
static volatile uint8_t s_clear_sd_pending = 0;

/** @brief 清除进度: -1=空闲, 0~99=进行中, 100=完成 */
static volatile int8_t s_clear_sd_progress = -1;

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
    "DIS_OFFSET", "CANALS_TYPE", "CHANNEL_ID", "CHANNEL_WIDTH", "WEIR_HEIGHT", "INSTANT_UNIT", "SUM_POINT",
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
 * @brief 请求清除SD卡所有数据 (线程安全)
 */
void app_log_request_clear_sd(void)
{
    s_clear_sd_progress = 0;
    s_clear_sd_pending = 1;
}

/**
 * @brief 获取SD卡清除进度
 */
int8_t app_log_get_clear_sd_progress(void)
{
    return s_clear_sd_progress;
}

typedef struct {
    uint8_t base;
    uint8_t cap;
    uint32_t ops;
    uint32_t deleted;
} clear_sd_walk_t;

static void clear_sd_set_progress(int progress)
{
    if (progress > 99) progress = 99;
    if (progress < 0) progress = 0;
    s_clear_sd_progress = (int8_t)progress;
}

static void clear_sd_step_progress(clear_sd_walk_t *walk)
{
    int progress;

    if (walk == NULL) return;

    walk->ops++;
    progress = (int)walk->base + (int)(walk->ops / 4U);
    if (progress > (int)walk->cap) progress = walk->cap;
    if (progress > s_clear_sd_progress) {
        clear_sd_set_progress(progress);
    }
}

static FRESULT clear_sd_delete_tree(const char *path, uint8_t delete_self, clear_sd_walk_t *walk)
{
    DIR dir;
    FILINFO fno;
    FRESULT res;
    FRESULT final_res = FR_OK;
    char child[96];

    HAL_IWDG_Refresh(&hiwdg);
    res = f_opendir(&dir, path);
    HAL_IWDG_Refresh(&hiwdg);
    if (res == FR_NO_PATH || res == FR_NO_FILE) {
        return FR_OK;
    }
    if (res != FR_OK) {
        printf("clear_sd: open %s failed (%d)\r\n", path, res);
        return res;
    }

    while (1) {
        HAL_IWDG_Refresh(&hiwdg);
        res = f_readdir(&dir, &fno);
        HAL_IWDG_Refresh(&hiwdg);
        if (res != FR_OK) {
            printf("clear_sd: read %s failed (%d)\r\n", path, res);
            final_res = res;
            break;
        }
        if (fno.fname[0] == '\0') {
            break;
        }
        if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) {
            continue;
        }

        if (snprintf(child, sizeof(child), "%s/%s", path, fno.fname) >= (int)sizeof(child)) {
            printf("clear_sd: path too long %s/%s\r\n", path, fno.fname);
            final_res = FR_INVALID_NAME;
            continue;
        }

        if (fno.fattrib & AM_DIR) {
            FRESULT child_res = clear_sd_delete_tree(child, 1, walk);
            if (child_res != FR_OK) {
                final_res = child_res;
            }
        } else {
            FRESULT unlink_res = f_unlink(child);
            if (unlink_res == FR_OK) {
                if (walk) walk->deleted++;
            } else {
                printf("clear_sd: del %s failed (%d)\r\n", child, unlink_res);
                final_res = unlink_res;
            }
            HAL_IWDG_Refresh(&hiwdg);
            clear_sd_step_progress(walk);
        }
    }

    f_closedir(&dir);

    if (delete_self && final_res == FR_OK) {
        FRESULT unlink_res = f_unlink(path);
        if (unlink_res == FR_OK) {
            if (walk) walk->deleted++;
        } else if (unlink_res != FR_NO_PATH && unlink_res != FR_NO_FILE) {
            printf("clear_sd: del dir %s failed (%d)\r\n", path, unlink_res);
            final_res = unlink_res;
        }
        HAL_IWDG_Refresh(&hiwdg);
        clear_sd_step_progress(walk);
    }

    return final_res;
}

static uint8_t clear_sd_dir_has_entries(const char *path)
{
    DIR dir;
    FILINFO fno;
    FRESULT res;

    HAL_IWDG_Refresh(&hiwdg);
    res = f_opendir(&dir, path);
    HAL_IWDG_Refresh(&hiwdg);

    if (res == FR_NO_PATH || res == FR_NO_FILE) {
        return 0;
    }
    if (res != FR_OK) {
        printf("clear_sd: check %s failed (%d)\r\n", path, res);
        return 1;
    }

    while (1) {
        HAL_IWDG_Refresh(&hiwdg);
        res = f_readdir(&dir, &fno);
        HAL_IWDG_Refresh(&hiwdg);
        if (res != FR_OK) {
            f_closedir(&dir);
            return 1;
        }
        if (fno.fname[0] == '\0') {
            f_closedir(&dir);
            return 0;
        }
        if (strcmp(fno.fname, ".") != 0 && strcmp(fno.fname, "..") != 0) {
            f_closedir(&dir);
            return 1;
        }
    }
}

static uint8_t clear_sd_unlink_existing(const char *path)
{
    FILINFO fno;
    FRESULT res;

    HAL_IWDG_Refresh(&hiwdg);
    res = f_stat(path, &fno);
    HAL_IWDG_Refresh(&hiwdg);

    if (res == FR_NO_PATH || res == FR_NO_FILE) {
        return 0;
    }
    if (res != FR_OK) {
        return 0;
    }

    res = f_unlink(path);
    HAL_IWDG_Refresh(&hiwdg);
    return (res == FR_OK) ? 1 : 0;
}

static void clear_sd_try_known_path(const char *path, uint32_t *deleted,
                                    uint32_t *attempts, uint32_t total,
                                    int progress_base, int progress_cap)
{
    int progress;

    if (deleted) {
        *deleted += clear_sd_unlink_existing(path);
    }

    if (attempts == NULL || total == 0U) return;

    (*attempts)++;
    if (((*attempts & 0x0FU) == 0U) || *attempts >= total) {
        progress = progress_base +
                   (int)((*attempts * (uint32_t)(progress_cap - progress_base)) / total);
        clear_sd_set_progress(progress);
    }
}

static uint32_t clear_sd_delete_known_paths(int end_year, int progress_base, int progress_cap)
{
    char filepath[64];
    uint32_t deleted = 0;
    uint32_t attempts = 0;
    uint32_t total_attempts;

    static const char *log_day_fmt[]   = { LOG_SYS_DAY_FILE,   LOG_USER_DAY_FILE,   LOG_ALARM_DAY_FILE   };
    static const char *log_month_fmt[] = { LOG_SYS_MONTH_DIR,  LOG_USER_MONTH_DIR,  LOG_ALARM_MONTH_DIR  };
    static const char *log_year_fmt[]  = { LOG_SYS_YEAR_DIR,   LOG_USER_YEAR_DIR,   LOG_ALARM_YEAR_DIR   };

    if (end_year < 2024) end_year = 2024;
    if (end_year > 2099) end_year = 2099;

    total_attempts = (uint32_t)(end_year - 2024 + 1) * 1540U + 5U;

    for (int type = 0; type < 3; type++) {
        for (int y = 2024; y <= end_year; y++) {
            for (int m = 1; m <= 12; m++) {
                for (int d = 1; d <= 31; d++) {
                    snprintf(filepath, sizeof(filepath), log_day_fmt[type], y, m, d);
                    clear_sd_try_known_path(filepath, &deleted, &attempts, total_attempts,
                                            progress_base, progress_cap);
                }
                snprintf(filepath, sizeof(filepath), log_month_fmt[type], y, m);
                clear_sd_try_known_path(filepath, &deleted, &attempts, total_attempts,
                                        progress_base, progress_cap);
            }
            snprintf(filepath, sizeof(filepath), log_year_fmt[type], y);
            clear_sd_try_known_path(filepath, &deleted, &attempts, total_attempts,
                                    progress_base, progress_cap);
        }
    }
    clear_sd_try_known_path(LOG_SYS_PATH, &deleted, &attempts, total_attempts,
                            progress_base, progress_cap);
    clear_sd_try_known_path(LOG_USER_PATH, &deleted, &attempts, total_attempts,
                            progress_base, progress_cap);
    clear_sd_try_known_path(LOG_ALARM_PATH, &deleted, &attempts, total_attempts,
                            progress_base, progress_cap);
    clear_sd_try_known_path(LOG_BASE_PATH, &deleted, &attempts, total_attempts,
                            progress_base, progress_cap);

    for (int y = 2024; y <= end_year; y++) {
        for (int m = 1; m <= 12; m++) {
            for (int d = 1; d <= 31; d++) {
                snprintf(filepath, sizeof(filepath), DATA_DAY_FILE, y, m, d);
                clear_sd_try_known_path(filepath, &deleted, &attempts, total_attempts,
                                        progress_base, progress_cap);
            }
            snprintf(filepath, sizeof(filepath), DATA_MONTH_DIR, y, m);
            clear_sd_try_known_path(filepath, &deleted, &attempts, total_attempts,
                                    progress_base, progress_cap);
        }
        snprintf(filepath, sizeof(filepath), DATA_YEAR_DIR, y);
        clear_sd_try_known_path(filepath, &deleted, &attempts, total_attempts,
                                progress_base, progress_cap);
    }
    clear_sd_try_known_path(DATA_BASE_PATH, &deleted, &attempts, total_attempts,
                            progress_base, progress_cap);

    return deleted;
}

/**
 * @brief 自包含SD卡数据清除 (在log_task中调用)
 * @note Recursively deletes existing /LOGS and /data entries.
 */
static void do_clear_sd(void)
{
    clear_sd_walk_t walk;
    uint32_t deleted_total = 0;
    RTC_TimeData t;
    uint8_t logs_left;
    uint8_t data_left;

    printf("clear_sd: start recursive delete\r\n");

    if (file_init() != FILE_OK) {
        printf("clear_sd: file init failed\r\n");
        s_clear_sd_progress = 100;
        return;
    }

    log_manager_deinit();
    data_recorder_deinit();

    walk.base = 1;
    walk.cap = 55;
    walk.ops = 0;
    walk.deleted = 0;
    clear_sd_set_progress(walk.base);
    (void)clear_sd_delete_tree(LOG_BASE_PATH, 1, &walk);
    printf("clear_sd: logs deleted %lu entries\r\n", (unsigned long)walk.deleted);
    deleted_total += walk.deleted;
    clear_sd_set_progress(55);

    walk.base = 55;
    walk.cap = 95;
    walk.ops = 0;
    walk.deleted = 0;
    (void)clear_sd_delete_tree(DATA_BASE_PATH, 1, &walk);
    printf("clear_sd: data deleted %lu entries\r\n", (unsigned long)walk.deleted);
    deleted_total += walk.deleted;
    clear_sd_set_progress(95);

    logs_left = clear_sd_dir_has_entries(LOG_BASE_PATH);
    data_left = clear_sd_dir_has_entries(DATA_BASE_PATH);
    if (logs_left || data_left) {
        RTC_Time_Get(&t);
        printf("clear_sd: fallback date unlink to %u\r\n", t.year);
        deleted_total += clear_sd_delete_known_paths((int)t.year, 95, 99);
        printf("clear_sd: fallback deleted, total %lu entries\r\n", (unsigned long)deleted_total);
    }

    HAL_IWDG_Refresh(&hiwdg);
    log_manager_init(NULL);
    HAL_IWDG_Refresh(&hiwdg);
    data_recorder_init(NULL);
    HAL_IWDG_Refresh(&hiwdg);
    history_invalidate_cache();

    s_clear_sd_progress = 100;
    printf("clear_sd: done\r\n");
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

    /* 处理清除SD卡数据请求 */
    if (s_clear_sd_pending) {
        s_clear_sd_pending = 0;
        s_clear_sd_progress = 0;
        do_clear_sd();
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
