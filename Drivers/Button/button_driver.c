/**
 * @file button_driver.c
 * @brief 按键驱动源文件
 * @note 支持4个按键：确认、上、下、位移
 *       支持短按(<2秒)和长按(>=2秒)检测，长按到达阈值立即触发
 */

#include "button_driver.h"

/* 长按阈值: 2000毫秒 */
#define LONG_PRESS_THRESHOLD_MS  2000

/* 消抖时间: 20毫秒 */
#define DEBOUNCE_TIME_MS         20

/* 按键数组 */
static Button_t g_buttons[BUTTON_ID_MAX];

/* 用户回调函数 */
static ButtonCallback_t g_button_callback = NULL;

/* 按键硬件配置 - 从main.h获取 */
static const uint16_t g_button_pins[BUTTON_ID_MAX] = {
    KEY_1_Pin,  /* 确认键 */
    KEY_2_Pin,  /* 上键 */
    KEY_3_Pin,  /* 下键 */
    KEY_4_Pin   /* 位移键 */
};

static GPIO_TypeDef *g_button_ports[BUTTON_ID_MAX] = {
    KEY_1_GPIO_Port,  /* 确认键 */
    KEY_2_GPIO_Port,  /* 上键 */
    KEY_3_GPIO_Port,  /* 下键 */
    KEY_4_GPIO_Port   /* 位移键 */
};

/**
 * @brief 读取按键GPIO电平
 * @param button_id: 按键ID
 * @retval true: 按下(低电平), false: 未按下(高电平)
 * @note 按键按下为低电平(根据硬件设计)
 */
static bool button_read_gpio(ButtonId_e button_id)
{
    return (HAL_GPIO_ReadPin(g_button_ports[button_id], g_button_pins[button_id]) == GPIO_PIN_RESET);
}

/**
 * @brief 初始化按键驱动
 * @param callback: 按键事件回调函数
 * @retval None
 */
void button_driver_init(ButtonCallback_t callback)
{
    ButtonId_e i;

    /* 保存回调函数 */
    g_button_callback = callback;

    /* 初始化按键结构体 */
    for (i = BUTTON_ID_OK; i < BUTTON_ID_MAX; i++) {
        g_buttons[i].port = g_button_ports[i];
        g_buttons[i].pin = g_button_pins[i];
        g_buttons[i].state = BUTTON_STATE_IDLE;
        g_buttons[i].press_time = 0;
        g_buttons[i].debounce_time = 0;
        g_buttons[i].long_triggered = false;
    }
}

/**
 * @brief 按键扫描任务
 * @param interval_ms: 调用间隔(毫秒)
 * @retval None
 * @note 需在定时器中断或FreeRTOS任务中周期性调用，建议10ms周期
 */
void button_driver_scan(uint32_t interval_ms)
{
    ButtonId_e i;
    bool is_pressed;
    ButtonEvent_e event = BUTTON_EVENT_NONE;

    for (i = BUTTON_ID_OK; i < BUTTON_ID_MAX; i++) {
        is_pressed = button_read_gpio(i);

        switch (g_buttons[i].state) {
            case BUTTON_STATE_IDLE:
                if (is_pressed) {
                    /* 检测到按下，进入消抖状态 */
                    g_buttons[i].state = BUTTON_STATE_DEBOUNCE;
                    g_buttons[i].debounce_time = 0;
                }
                break;

            case BUTTON_STATE_DEBOUNCE:
                if (is_pressed) {
                    /* 持续按下，累加消抖时间 */
                    g_buttons[i].debounce_time += interval_ms;

                    /* 消抖时间到，确认按下 */
                    if (g_buttons[i].debounce_time >= DEBOUNCE_TIME_MS) {
                        g_buttons[i].state = BUTTON_STATE_PRESSED;
                        g_buttons[i].press_time = 0;
                        g_buttons[i].long_triggered = false;
                    }
                } else {
                    /* 抖动恢复，重置状态 */
                    g_buttons[i].state = BUTTON_STATE_IDLE;
                    g_buttons[i].debounce_time = 0;
                }
                break;

            case BUTTON_STATE_PRESSED:
                if (is_pressed) {
                    /* 持续按下，累加时间 */
                    g_buttons[i].press_time += interval_ms;

                    /* 检查是否达到长按阈值，到达立即触发 */
                    if (g_buttons[i].press_time >= LONG_PRESS_THRESHOLD_MS) {
                        g_buttons[i].long_triggered = true;
                        g_buttons[i].state = BUTTON_STATE_LONG_PRESSED;

                        if (g_button_callback != NULL) {
                            g_button_callback(i, BUTTON_EVENT_LONG);
                        }
                    }
                } else {
                    /* 松手，判断短按 */
                    if (g_buttons[i].press_time < LONG_PRESS_THRESHOLD_MS) {
                        /* 短按触发 */
                        event = BUTTON_EVENT_SHORT;
                    }
                    /* 重置按键状态 */
                    g_buttons[i].state = BUTTON_STATE_IDLE;
                    g_buttons[i].press_time = 0;
                    g_buttons[i].debounce_time = 0;

                    /* 触发回调 */
                    if (g_button_callback != NULL) {
                        g_button_callback(i, event);
                    }
                }
                break;

            case BUTTON_STATE_LONG_PRESSED:
                if (!is_pressed) {
                    /* 松手，判断长按 */
                    if (g_buttons[i].long_triggered) {
                        /* 长按触发 */
                        event = BUTTON_EVENT_LONG;
                    }
                    /* 重置按键状态 */
                    g_buttons[i].state = BUTTON_STATE_IDLE;
                    g_buttons[i].press_time = 0;
                    g_buttons[i].debounce_time = 0;
                    g_buttons[i].long_triggered = false;

                    /* 触发回调 */
                    if (g_button_callback != NULL) {
                        g_button_callback(i, event);
                    }
                }
                break;

            default:
                g_buttons[i].state = BUTTON_STATE_IDLE;
                break;
        }
    }
}

/**
 * @brief 获取按键当前按下状态
 * @param button_id: 按键ID
 * @retval true: 按下, false: 未按下
 */
bool button_get_pressed(ButtonId_e button_id)
{
    if (button_id >= BUTTON_ID_MAX) {
        return false;
    }

    return button_read_gpio(button_id);
}
