/**
 * @file button_driver.h
 * @brief 按键驱动头文件
 * @note 支持4个按键：确认、上、下、位移
 *       支持短按(<2秒)和长按(>=2秒)检测，长按到达阈值立即触发
 */

#ifndef __BUTTON_DRIVER_H
#define __BUTTON_DRIVER_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* 按键ID定义 */
typedef enum {
    BUTTON_ID_OK = 0,      /* 确认键 */
    BUTTON_ID_UP,          /* 上键 */
    BUTTON_ID_DOWN,        /* 下键 */
    BUTTON_ID_SHIFT,       /* 位移键 */
    BUTTON_ID_MAX
} ButtonId_e;

/* 按键事件类型 */
typedef enum {
    BUTTON_EVENT_SHORT = 0,    /* 短按事件 (<2秒) */
    BUTTON_EVENT_LONG,         /* 长按事件 (>=2秒) */
    BUTTON_EVENT_NONE
} ButtonEvent_e;

/* 按键状态 */
typedef enum {
    BUTTON_STATE_IDLE = 0,         /* 空闲 */
    BUTTON_STATE_DEBOUNCE,         /* 消抖中 */
    BUTTON_STATE_PRESSED,          /* 稳定按下 */
    BUTTON_STATE_LONG_PRESSED      /* 长按触发过 */
} ButtonState_e;

/* 按键配置结构体 */
typedef struct {
    GPIO_TypeDef *port;        /* GPIO端口 */
    uint16_t pin;              /* GPIO引脚 */
    ButtonState_e state;       /* 当前状态 */
    uint32_t press_time;       /* 按下持续时间(毫秒) */
    uint32_t debounce_time;    /* 消抖计时(毫秒) */
    bool long_triggered;        /* 长按是否已触发 */
} Button_t;

/* 按键事件回调函数类型 */
typedef void (*ButtonCallback_t)(ButtonId_e button_id, ButtonEvent_e event);

/**
 * @brief 初始化按键驱动
 * @param callback: 按键事件回调函数
 * @retval None
 */
void button_driver_init(ButtonCallback_t callback);

/**
 * @brief 按键扫描任务(需在定时器中断或任务中周期性调用)
 * @param interval_ms: 调用间隔(毫秒)
 * @retval None
 */
void button_driver_scan(uint32_t interval_ms);

/**
 * @brief 获取按键当前按下状态
 * @param button_id: 按键ID
 * @retval true: 按下, false: 未按下
 */
bool button_get_pressed(ButtonId_e button_id);

#endif /* __BUTTON_DRIVER_H */
