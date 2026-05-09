/**
 * @file ui_history.h
 * @brief 历史记录查询界面
 */

#ifndef __UI_HISTORY_H__
#define __UI_HISTORY_H__

#include <stdint.h>

/**
 * @brief 进入历史查询界面 (主页面长按SHIFT触发)
 */
void history_screen_enter(void);

/**
 * @brief 退出历史查询界面，返回主屏幕
 */
void history_screen_exit(void);

/**
 * @brief 历史页面按键处理
 * @param button_id BUTTON_ID_OK/UP/DOWN/SHIFT
 * @param event BUTTON_EVENT_SHORT/LONG
 */
void history_button_handler(uint8_t button_id, uint8_t event);

/**
 * @brief 历史查询处理 (在log_task中调用，执行文件I/O)
 */
void history_query_process(void);

/**
 * @brief 使历史查询缓存失效 (例如清空SD卡数据后调用)
 */
void history_invalidate_cache(void);

#endif /* __UI_HISTORY_H__ */
