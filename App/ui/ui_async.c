/**
 * @file    ui_async.c
 * @brief   线程安全的异步UI操作实现
 *
 * 已弃用: 使用 LVGL 原生的 lv_async_call() 替代。
 * 本文件保留仅为兼容 Keil 工程配置，不再使用。
 */

#include "ui_async.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <string.h>

#define UI_ASYNC_QUEUE_SIZE     8
#define UI_ASYNC_MAX_DATA_SIZE  16

typedef struct {
    lv_async_cb_t cb;
    uint16_t data_size;
    uint8_t data[UI_ASYNC_MAX_DATA_SIZE];
} async_item_t;

static QueueHandle_t s_async_queue;

void ui_async_init(void)
{
    s_async_queue = xQueueCreate(UI_ASYNC_QUEUE_SIZE, sizeof(async_item_t));
}

int ui_async_call(lv_async_cb_t cb, const void *data, uint16_t data_size)
{
    if (cb == NULL) return -1;
    if (data_size > UI_ASYNC_MAX_DATA_SIZE) return -1;

    async_item_t item;
    item.cb = cb;
    item.data_size = data_size;

    if (data && data_size > 0) {
        memcpy(item.data, data, data_size);
    } else {
        item.data_size = 0;
    }

    if (xQueueSend(s_async_queue, &item, 0) != pdPASS) {
        return -1;
    }
    return 0;
}

void ui_async_process(void)
{
    async_item_t item;

    while (xQueueReceive(s_async_queue, &item, 0) == pdPASS) {
        void *ctx = NULL;

        if (item.data_size > 0) {
            ctx = lv_malloc(item.data_size);
            if (ctx == NULL) continue;
            memcpy(ctx, item.data, item.data_size);
        }

        item.cb(ctx);
    }
}
