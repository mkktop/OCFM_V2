/**
 * @file    ui_async.h
 * @brief   线程安全的异步UI操作接口
 * @details 解决按键任务直接调用 lv_async_call() / lv_malloc() 导致的线程安全问题。
 *          使用 FreeRTOS 队列将异步请求从按键任务传递到 LVGL 主任务执行。
 *
 *          用法:
 *            1. 系统启动时调用 ui_async_init()
 *            2. 按键任务中调用 ui_async_call() 替代 lv_async_call() + lv_malloc()
 *            3. LVGL 主任务循环中调用 ui_async_process()
 */

#ifndef UI_ASYNC_H
#define UI_ASYNC_H

#include "lvgl.h"
#include <stdint.h>

void ui_async_init(void);
void ui_async_process(void);

/**
 * @brief  线程安全的异步UI调用 (替代 lv_async_call)
 * @param  cb: 回调函数 (在 LVGL 主任务上下文执行)
 * @param  data: 上下文数据指针 (会被拷贝到队列，调用者可用栈上变量)
 * @param  data_size: 上下文数据大小 (不超过 UI_ASYNC_MAX_DATA_SIZE)
 * @retval 0=成功, -1=队列满或参数无效
 * @note   回调函数收到的 data 指针由 lv_malloc 分配，需在回调末尾 lv_free()
 */
int ui_async_call(lv_async_cb_t cb, const void *data, uint16_t data_size);

#endif /* UI_ASYNC_H */
