/**
 * @file    button_driver.h
 * @brief   按键驱动头文件 - 支持短按、长按、重复按
 * @details 基于STM32F407VG单片机的4个GPIO按键输入，提供完善的事件回调机制
 * 
 * @attention
 * 单片机: STM32F407VGTx
 * LVGL版本: 9.5.0
 */

#ifndef __BUTTON_DRIVER_H
#define __BUTTON_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/*============================================================================*/
/*                           按键硬件定义                                       */
/*============================================================================*/

/**
 * @brief 按键IO定义
 * @note  根据原理图确定按键是高电平有效还是低电平有效
 *        通常按键未按下为高电平，按下为低电平(低电平有效)
 */
typedef enum
{
    BTN_1 = 0,    /* 按键1 - GPIOA Pin15 */
    BTN_2,        /* 按键2 - GPIOA Pin12 */
    BTN_3,        /* 按键3 - GPIOA Pin8 */
    BTN_4,        /* 按键4 - GPIOC Pin6 */
    BTN_MAX       /* 按键数量 */
} button_id_t;

/**
 * @brief 按键有效电平
 */
typedef enum
{
    BTN_LEVEL_LOW = 0,    /* 低电平有效(按下为低) */
    BTN_LEVEL_HIGH        /* 高电平有效(按下为高) */
} button_level_t;

/**
 * @brief 按键事件类型
 */
typedef enum
{
    BTN_EVENT_NONE = 0,       /* 无事件 */
    BTN_EVENT_PRESS,          /* 按下 */
    BTN_EVENT_RELEASE,        /* 释放 */
    BTN_EVENT_SHORT_PRESS,    /* 短按 */
    BTN_EVENT_LONG_PRESS      /* 长按 */
} button_event_t;

/**
 * @brief 按键状态
 */
typedef enum
{
    BTN_STATE_IDLE = 0,       /* 空闲 */
    BTN_STATE_PRESSED,        /* 已按下 */
    BTN_STATE_LONG_PRESS,     /* 长按中 */
    BTN_STATE_RELEASED        /* 已释放 */
} button_state_t;

/*============================================================================*/
/*                           按键配置参数                                       */
/*============================================================================*/

/**
 * @brief 按键消抖时间 (毫秒)
 * @note  消除按键机械抖动，通常5-20ms
 */
#define BTN_DEBOUNCE_TIME      20

/**
 * @brief 短按判定时间 (毫秒)
 * @note  按下时间小于此值为短按
 */
#define BTN_SHORT_PRESS_TIME   50

/**
 * @brief 长按判定时间 (毫秒)
 * @note  按下时间超过此值判定为长按
 */
#define BTN_LONG_PRESS_TIME    1000

/**
 * @brief 按键数量
 */
#define BUTTON_MAX_NUM          4

/*============================================================================*/
/*                           数据结构定义                                       */
/*============================================================================*/

/**
 * @brief 按键IO配置结构体
 */
typedef struct
{
    GPIO_TypeDef *port;        /* GPIO端口 */
    uint16_t pin;              /* GPIO引脚 */
    button_level_t active_level;  /* 有效电平 */
} button_gpio_t;

/**
 * @brief 按键统计信息
 */
typedef struct
{
    uint32_t press_count;      /* 按下次数 */
    uint32_t short_press_count;/* 短按次数 */
    uint32_t long_press_count; /* 长按次数 */
    uint32_t release_count;    /* 释放次数 */
} button_stats_t;

/**
 * @brief 按键对象结构体
 */
typedef struct button_obj
{
    button_id_t id;                    /* 按键ID */
    button_state_t state;               /* 当前状态 */
    button_event_t last_event;         /* 最后一次事件 */
    uint32_t press_start_time;          /* 按下开始时间 */
    uint32_t last_release_time;         /* 上次释放时间 */
    uint32_t repeat_count;              /* 长按重复次数 */
    bool enable;                        /* 使能标志 */
    button_stats_t stats;               /* 统计信息 */
    
    /* 回调函数 */
    void (*on_press)(button_id_t id);           /* 按下回调 */
    void (*on_release)(button_id_t id);         /* 释放回调 */
    void (*on_short_press)(button_id_t id);     /* 短按回调 */
    void (*on_long_press)(button_id_t id);      /* 长按回调 */
    
    struct button_obj *next;           /* 链表下一个节点 */
} button_obj_t;

/**
 * @brief 按键管理器配置
 */
typedef struct
{
    uint32_t scan_interval;             /* 扫描间隔(ms) */
} button_manager_config_t;

/**
 * @brief 按键管理器结构体
 */
typedef struct
{
    button_obj_t buttons[BUTTON_MAX_NUM];   /* 按键对象数组 */
    button_manager_config_t config;         /* 配置信息 */
    uint32_t system_time;                   /* 系统时间(毫秒) */
    uint8_t initialized;                    /* 初始化标志 */
} button_manager_t;

/*============================================================================*/
/*                           API函数声明                                       */
/*============================================================================*/

/**
 * @brief  初始化按键驱动
 * @param  manager: 按键管理器指针
 * @note   初始化所有按键IO和状态
 */
void button_driver_init(button_manager_t *manager);

/**
 * @brief  按键扫描任务(需周期性调用)
 * @param  manager: 按键管理器指针
 * @note   建议在10ms周期定时器中调用
 */
void button_driver_scan(button_manager_t *manager);

/**
 * @brief  更新系统时间
 * @param  manager: 按键管理器指针
 * @param  ms: 当前系统时间(毫秒)
 * @note   需外部提供毫秒级时间基准
 */
void button_driver_update_time(button_manager_t *manager, uint32_t ms);

/**
 * @brief  获取按键当前状态
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval 按键状态
 */
button_state_t button_driver_get_state(button_manager_t *manager, button_id_t id);

/**
 * @brief  获取按键事件
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval 最后一次按键事件
 */
button_event_t button_driver_get_event(button_manager_t *manager, button_id_t id);

/**
 * @brief  检查按键是否按下
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval true-按下 false-未按下
 */
bool button_driver_is_pressed(button_manager_t *manager, button_id_t id);

/**
 * @brief  检查按键是否长按
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval true-长按中 false-非长按
 */
bool button_driver_is_long_pressed(button_manager_t *manager, button_id_t id);

/**
 * @brief  使能/禁用按键
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  enable: true-使能 false-禁用
 */
void button_driver_enable(button_manager_t *manager, button_id_t id, bool enable);

/**
 * @brief  注册按键按下回调
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  callback: 回调函数
 */
void button_driver_register_press_cb(button_manager_t *manager, button_id_t id, 
                                     void (*callback)(button_id_t id));

/**
 * @brief  注册按键释放回调
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  callback: 回调函数
 */
void button_driver_register_release_cb(button_manager_t *manager, button_id_t id,
                                       void (*callback)(button_id_t id));

/**
 * @brief  注册短按回调
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  callback: 回调函数
 */
void button_driver_register_short_press_cb(button_manager_t *manager, button_id_t id,
                                           void (*callback)(button_id_t id));

/**
 * @brief  注册长按回调
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  callback: 回调函数
 */
void button_driver_register_long_press_cb(button_manager_t *manager, button_id_t id,
                                            void (*callback)(button_id_t id));

/**
 * @brief  获取按键统计信息
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval 统计信息结构体指针
 */
button_stats_t* button_driver_get_stats(button_manager_t *manager, button_id_t id);

/**
 * @brief  重置按键统计
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 */
void button_driver_reset_stats(button_manager_t *manager, button_id_t id);

/**
 * @brief  获取按键GPIO状态(原始读取)
 * @param  id: 按键ID
 * @retval true-高电平 false-低电平
 */
bool button_driver_read_gpio(button_id_t id);

/**
 * @brief  LVGL输入设备初始化
 * @note   初始化LVGL的按键输入驱动，用于触摸操作
 */
void button_lvgl_init(void);

/**
 * @brief  LVGL按键扫描(在LVGL任务中调用)
 * @note   将按键事件转换为LVGL输入
 */
void button_lvgl_scan(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUTTON_DRIVER_H */
