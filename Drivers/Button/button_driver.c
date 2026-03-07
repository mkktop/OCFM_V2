/**
 * @file    button_driver.c
 * @brief   按键驱动实现文件 - 支持短按、长按
 * @details 实现GPIO按键扫描，提供完善的事件回调机制
 * 
 * @attention
 * 单片机: STM32F407VGTx
 * LVGL版本: 9.5.0
 */

#include "button_driver.h"

/*============================================================================*/
/*                           私有常量定义                                       */
/*============================================================================*/

/**
 * @brief 按键GPIO配置表
 * @note 根据原理图配置: 按键按下为低电平
 */
static const button_gpio_t button_gpio_table[BUTTON_MAX_NUM] = {
    {GPIOA, GPIO_PIN_15, BTN_LEVEL_LOW},  /* 按键1: PA15 */
    {GPIOA, GPIO_PIN_12, BTN_LEVEL_LOW},  /* 按键2: PA12 */
    {GPIOA, GPIO_PIN_8, BTN_LEVEL_LOW},   /* 按键3: PA8 */
    {GPIOC, GPIO_PIN_6, BTN_LEVEL_LOW}    /* 按键4: PC6 */
};

/*============================================================================*/
/*                           私有函数声明                                       */
/*============================================================================*/

/**
 * @brief  读取按键GPIO状态
 * @param  btn: 按键对象指针
 * @retval true-按下 false-未按下
 */
static bool button_read(button_obj_t *btn);

/**
 * @brief  处理按键按下事件
 * @param  btn: 按键对象指针
 */
static void button_handle_press(button_obj_t *btn);

/**
 * @brief  处理按键释放事件
 * @param  btn: 按键对象指针
 */
static void button_handle_release(button_obj_t *btn);

/**
 * @brief  处理长按超时事件
 * @param  btn: 按键对象指针
 */
static void button_handle_long_press_timeout(button_obj_t *btn);

/*============================================================================*/
/*                           公共函数实现                                       */
/*============================================================================*/

/**
 * @brief  初始化按键驱动
 * @param  manager: 按键管理器指针
 */
void button_driver_init(button_manager_t *manager)
{
    if (manager == NULL || manager->initialized) {
        return;
    }

    /* 初始化配置 */
    manager->config.scan_interval = 10;
    manager->system_time = 0;
    manager->initialized = 1;

    /* 初始化每个按键 */
    for (int i = 0; i < BUTTON_MAX_NUM; i++) {
        button_obj_t *btn = &manager->buttons[i];
        
        btn->id = (button_id_t)i;
        btn->state = BTN_STATE_IDLE;
        btn->last_event = BTN_EVENT_NONE;
        btn->press_start_time = 0;
        btn->enable = 1;
        
        /* 初始化统计 */
        btn->stats.press_count = 0;
        btn->stats.short_press_count = 0;
        btn->stats.long_press_count = 0;
        btn->stats.release_count = 0;
        
        /* 初始化回调函数 */
        btn->on_press = NULL;
        btn->on_release = NULL;
        btn->on_short_press = NULL;
        btn->on_long_press = NULL;
        btn->next = NULL;
    }
}

/**
 * @brief  按键扫描任务
 * @param  manager: 按键管理器指针
 * @note   建议在10ms周期定时器中调用
 */
void button_driver_scan(button_manager_t *manager)
{
    if (manager == NULL || !manager->initialized) {
        return;
    }

    /* 扫描每个按键 */
    for (int i = 0; i < BUTTON_MAX_NUM; i++) {
        button_obj_t *btn = &manager->buttons[i];
        
        if (!btn->enable) {
            continue;
        }

        bool pressed = button_read(btn);
        
        switch (btn->state) {
            case BTN_STATE_IDLE:
                if (pressed) {
                    button_handle_press(btn);
                }
                break;
                
            case BTN_STATE_PRESSED:
                if (!pressed) {
                    button_handle_release(btn);
                } else {
                    button_handle_long_press_timeout(btn);
                }
                break;
                
            case BTN_STATE_LONG_PRESS:
                if (!pressed) {
                    button_handle_release(btn);
                }
                break;
                
            case BTN_STATE_RELEASED:
                btn->state = BTN_STATE_IDLE;
                btn->last_event = BTN_EVENT_NONE;
                break;
                
            default:
                btn->state = BTN_STATE_IDLE;
                break;
        }
    }
}

/**
 * @brief  更新系统时间
 * @param  manager: 按键管理器指针
 * @param  ms: 当前系统时间(毫秒)
 */
void button_driver_update_time(button_manager_t *manager, uint32_t ms)
{
    if (manager != NULL) {
        manager->system_time = ms;
    }
}

/**
 * @brief  获取按键当前状态
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval 按键状态
 */
button_state_t button_driver_get_state(button_manager_t *manager, button_id_t id)
{
    if (manager == NULL || id >= BTN_MAX) {
        return BTN_STATE_IDLE;
    }
    return manager->buttons[id].state;
}

/**
 * @brief  获取按键事件
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval 最后一次按键事件
 */
button_event_t button_driver_get_event(button_manager_t *manager, button_id_t id)
{
    if (manager == NULL || id >= BTN_MAX) {
        return BTN_EVENT_NONE;
    }
    return manager->buttons[id].last_event;
}

/**
 * @brief  检查按键是否按下
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval true-按下 false-未按下
 */
bool button_driver_is_pressed(button_manager_t *manager, button_id_t id)
{
    if (manager == NULL || id >= BTN_MAX) {
        return false;
    }
    return (manager->buttons[id].state == BTN_STATE_PRESSED) ||
           (manager->buttons[id].state == BTN_STATE_LONG_PRESS);
}

/**
 * @brief  检查按键是否长按
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval true-长按中 false-非长按
 */
bool button_driver_is_long_pressed(button_manager_t *manager, button_id_t id)
{
    if (manager == NULL || id >= BTN_MAX) {
        return false;
    }
    return (manager->buttons[id].state == BTN_STATE_LONG_PRESS);
}

/**
 * @brief  使能/禁用按键
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  enable: true-使能 false-禁用
 */
void button_driver_enable(button_manager_t *manager, button_id_t id, bool enable)
{
    if (manager == NULL || id >= BTN_MAX) {
        return;
    }
    manager->buttons[id].enable = enable;
    
    if (!enable) {
        manager->buttons[id].state = BTN_STATE_IDLE;
        manager->buttons[id].last_event = BTN_EVENT_NONE;
    }
}

/**
 * @brief  注册按键按下回调
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  callback: 回调函数
 */
void button_driver_register_press_cb(button_manager_t *manager, button_id_t id,
                                     void (*callback)(button_id_t id))
{
    if (manager == NULL || id >= BTN_MAX || callback == NULL) {
        return;
    }
    manager->buttons[id].on_press = callback;
}

/**
 * @brief  注册按键释放回调
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  callback: 回调函数
 */
void button_driver_register_release_cb(button_manager_t *manager, button_id_t id,
                                       void (*callback)(button_id_t id))
{
    if (manager == NULL || id >= BTN_MAX || callback == NULL) {
        return;
    }
    manager->buttons[id].on_release = callback;
}

/**
 * @brief  注册短按回调
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  callback: 回调函数
 */
void button_driver_register_short_press_cb(button_manager_t *manager, button_id_t id,
                                           void (*callback)(button_id_t id))
{
    if (manager == NULL || id >= BTN_MAX || callback == NULL) {
        return;
    }
    manager->buttons[id].on_short_press = callback;
}

/**
 * @brief  注册长按回调
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @param  callback: 回调函数
 */
void button_driver_register_long_press_cb(button_manager_t *manager, button_id_t id,
                                           void (*callback)(button_id_t id))
{
    if (manager == NULL || id >= BTN_MAX || callback == NULL) {
        return;
    }
    manager->buttons[id].on_long_press = callback;
}

/**
 * @brief  获取按键统计信息
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 * @retval 统计信息结构体指针
 */
button_stats_t* button_driver_get_stats(button_manager_t *manager, button_id_t id)
{
    if (manager == NULL || id >= BTN_MAX) {
        return NULL;
    }
    return &manager->buttons[id].stats;
}

/**
 * @brief  重置按键统计
 * @param  manager: 按键管理器指针
 * @param  id: 按键ID
 */
void button_driver_reset_stats(button_manager_t *manager, button_id_t id)
{
    if (manager == NULL || id >= BTN_MAX) {
        return;
    }
    manager->buttons[id].stats.press_count = 0;
    manager->buttons[id].stats.short_press_count = 0;
    manager->buttons[id].stats.long_press_count = 0;
    manager->buttons[id].stats.release_count = 0;
}

/**
 * @brief  读取按键GPIO状态
 * @param  id: 按键ID
 * @retval true-高电平 false-低电平
 */
bool button_driver_read_gpio(button_id_t id)
{
    if (id >= BTN_MAX) {
        return false;
    }
    
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(
        (GPIO_TypeDef*)button_gpio_table[id].port,
        button_gpio_table[id].pin
    );
    
    if (button_gpio_table[id].active_level == BTN_LEVEL_LOW) {
        return (pin_state == GPIO_PIN_RESET);
    } else {
        return (pin_state == GPIO_PIN_SET);
    }
}

/*============================================================================*/
/*                           私有函数实现                                       */
/*============================================================================*/

/**
 * @brief  读取按键状态
 * @param  btn: 按键对象指针
 * @retval true-按下 false-未按下
 */
static bool button_read(button_obj_t *btn)
{
    return button_driver_read_gpio(btn->id);
}

/**
 * @brief  处理按键按下事件
 * @param  btn: 按键对象指针
 */
static void button_handle_press(button_obj_t *btn)
{
    btn->state = BTN_STATE_PRESSED;
    btn->press_start_time = 0;
    btn->last_event = BTN_EVENT_PRESS;
    btn->stats.press_count++;
    
    if (btn->on_press != NULL) {
        btn->on_press(btn->id);
    }
}

/**
 * @brief  处理按键释放事件
 * @param  btn: 按键对象指针
 */
static void button_handle_release(button_obj_t *btn)
{
    uint32_t press_time = 0;
    
    extern button_manager_t g_button_manager;
    if (g_button_manager.initialized) {
        press_time = g_button_manager.system_time - btn->press_start_time;
    }
    
    btn->stats.release_count++;
    
    if (press_time < BTN_SHORT_PRESS_TIME) {
        /* 抖动或无效按下 */
    } else if (press_time < BTN_LONG_PRESS_TIME) {
        /* 短按 */
        btn->last_event = BTN_EVENT_SHORT_PRESS;
        btn->stats.short_press_count++;
        
        if (btn->on_short_press != NULL) {
            btn->on_short_press(btn->id);
        }
    } else {
        /* 长按释放 */
        btn->last_event = BTN_EVENT_RELEASE;
        
        if (btn->on_release != NULL) {
            btn->on_release(btn->id);
        }
    }
    
    btn->state = BTN_STATE_RELEASED;
}

/**
 * @brief  处理长按超时事件
 * @param  btn: 按键对象指针
 */
static void button_handle_long_press_timeout(button_obj_t *btn)
{
    uint32_t press_time = 0;
    
    extern button_manager_t g_button_manager;
    if (g_button_manager.initialized) {
        press_time = g_button_manager.system_time - btn->press_start_time;
    }
    
    if (press_time >= BTN_LONG_PRESS_TIME && btn->state == BTN_STATE_PRESSED) {
        btn->state = BTN_STATE_LONG_PRESS;
        btn->last_event = BTN_EVENT_LONG_PRESS;
        btn->stats.long_press_count++;
        
        if (btn->on_long_press != NULL) {
            btn->on_long_press(btn->id);
        }
    }
}

/*============================================================================*/
/*                           LVGL联动相关                                       */
/*============================================================================*/

/**
 * @brief  LVGL输入设备初始化
 * @note   预留接口，可与LVGL输入设备驱动对接
 */
void button_lvgl_init(void)
{
}

/**
 * @brief  LVGL按键扫描
 * @note   预留接口
 */
void button_lvgl_scan(void)
{
}

/*============================================================================*/
/*                           全局变量定义                                       */
/*============================================================================*/

/**
 * @brief 全局按键管理器实例
 */
button_manager_t g_button_manager;

/**
 * @brief 简单延时函数
 */
void button_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}