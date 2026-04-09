/**
 * @file    ui_async.h
 * @brief   线程安全的异步UI操作接口
 *
 * 已弃用: 使用 LVGL 原生的 lv_async_call() 替代。
 * 本文件保留仅为兼容 Keil 工程配置，不再使用。
 */

#ifndef UI_ASYNC_H
#define UI_ASYNC_H

#include "lvgl.h"

void ui_async_init(void);
int ui_async_call(lv_async_cb_t cb, const void *data, uint16_t data_size);
void ui_async_process(void);

#endif /* UI_ASYNC_H */
