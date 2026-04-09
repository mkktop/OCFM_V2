/**
 * @file    app_alarm.c
 * @brief   报警模块实现 - 瞬时流量报警控制继电器
 * @details 基于瞬时流量(m?/h)触发四路继电器报警
 *          回差逻辑防止频繁触发/解除
 */

#include "app_alarm.h"
#include "app_config.h"
#include "app_log.h"
#include "main.h"
#include "gpio.h"
#include <math.h>
#include <stdio.h>

/*============================================================================*/
/*                           私有数据                                           */
/*============================================================================*/

/**
 * @brief 继电器GPIO映射表
 */
static const struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} relay_gpio_map[4] = {
    {RELAY1_CTRL_GPIO_Port, RELAY1_CTRL_Pin},  /**< 继电器1: PA4 */
    {RELAY2_CTRL_GPIO_Port, RELAY2_CTRL_Pin},  /**< 继电器2: PA5 */
    {RELAY3_CTRL_GPIO_Port, RELAY3_CTRL_Pin},  /**< 继电器3: PA6 */
    {RELAY4_CTRL_GPIO_Port, RELAY4_CTRL_Pin},  /**< 继电器4: PA7 */
};

/**
 * @brief 报警状态表 (当前各报警是否激活)
 */
static alarm_state_t alarm_states[ALARM_TYPE_COUNT] = {
    ALARM_STATE_NORMAL,  /**< 上限报警状态 */
    ALARM_STATE_NORMAL,  /**< 下限报警状态 */
    ALARM_STATE_NORMAL,  /**< 上上限报警状态 */
    ALARM_STATE_NORMAL   /**< 下下限报警状态 */
};

/*============================================================================*/
/*                           私有函数                                           */
/*============================================================================*/

/**
 * @brief  控制继电器输出
 * @param  relay_index: 继电器索引 (0-3, 对应继电器1-4)
 * @param  on: 1=ON(GPIO_PIN_SET), 0=OFF(GPIO_PIN_RESET)
 */
static void relay_set(uint8_t relay_index, uint8_t on)
{
    if (relay_index >= 4) {
        return;
    }

    GPIO_PinState state = (on) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(relay_gpio_map[relay_index].port,
                      relay_gpio_map[relay_index].pin,
                      state);
}

/*============================================================================*/
/*                           对外接口                                           */
/*============================================================================*/

/**
 * @brief  初始化报警模块
 * @note   启动时调用，初始化继电器为关闭状态
 */
void app_alarm_init(void)
{
    /* 初始化所有继电器为关闭状态 */
    for (uint8_t i = 0; i < 4; i++) {
        alarm_states[i] = ALARM_STATE_NORMAL;
        relay_set(i, 0);
    }
}

/**
 * @brief  更新报警判断 (每秒调用)
 * @param  instant_flow_m3h: 瞬时流量 (m?/h)
 * @note   报警逻辑说明：
 *         上限报警(AH)：流量超过AH触发继电器1，低于(AH-DH)解除
 *         下限报警(AL)：流量低于AL触发继电器2，高于(AL+DL)解除
 *         上上限报警(AAH)：流量超过AAH触发继电器3，低于AH解除
 *         下下限报警(AAL)：流量低于AAL触发继电器4，高于AL解除
 */
void app_alarm_update(float instant_flow_m3h)
{
    float ah, al, dh, dl, aah, aal;

    /* 获取报警阈值参数 (单位: m?/h) */
    ah = app_config_get_alarm_ah();
    al = app_config_get_alarm_al();
    dh = app_config_get_alarm_dh();
    dl = app_config_get_alarm_dl();
    aah = app_config_get_alarm_aah();
    aal = app_config_get_alarm_aal();

    /* ========== 上限报警 (继电器1) ========== */
    /* 触发条件: 流量 > AH
     * 解除条件: 流量 < AH - DH (回差) */
    if (alarm_states[ALARM_TYPE_AH] == ALARM_STATE_NORMAL) {
        /* 正常状态，检查是否需要触发 */
        if (instant_flow_m3h > ah) {
            alarm_states[ALARM_TYPE_AH] = ALARM_STATE_ACTIVE;
            relay_set(ALARM_TYPE_AH, 1);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AH ACTIVE flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    } else {
        /* 报警状态，检查是否需要解除 */
        if (instant_flow_m3h < (ah - dh)) {
            alarm_states[ALARM_TYPE_AH] = ALARM_STATE_NORMAL;
            relay_set(ALARM_TYPE_AH, 0);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AH NORMAL flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    }

    /* ========== 上上限报警 (继电器3) ========== */
    /* 触发条件: 流量 > AAH
     * 解除条件: 流量 < AH (跟随上限报警解除) */
    if (alarm_states[ALARM_TYPE_AAH] == ALARM_STATE_NORMAL) {
        if (instant_flow_m3h > aah) {
            alarm_states[ALARM_TYPE_AAH] = ALARM_STATE_ACTIVE;
            relay_set(ALARM_TYPE_AAH, 1);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AAH ACTIVE flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    } else {
        /* 上上限报警解除条件：流量降到上限报警值以下 */
        if (instant_flow_m3h < ah) {
            alarm_states[ALARM_TYPE_AAH] = ALARM_STATE_NORMAL;
            relay_set(ALARM_TYPE_AAH, 0);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AAH NORMAL flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    }

    /* ========== 下限报警 (继电器2) ========== */
    /* 触发条件: 流量 < AL
     * 解除条件: 流量 > AL + DL (回差) */
    if (alarm_states[ALARM_TYPE_AL] == ALARM_STATE_NORMAL) {
        /* 正常状态，检查是否需要触发 */
        if (instant_flow_m3h < al) {
            alarm_states[ALARM_TYPE_AL] = ALARM_STATE_ACTIVE;
            relay_set(ALARM_TYPE_AL, 1);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AL ACTIVE flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    } else {
        /* 报警状态，检查是否需要解除 */
        if (instant_flow_m3h > (al + dl)) {
            alarm_states[ALARM_TYPE_AL] = ALARM_STATE_NORMAL;
            relay_set(ALARM_TYPE_AL, 0);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AL NORMAL flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    }

    /* ========== 下下限报警 (继电器4) ========== */
    /* 触发条件: 流量 < AAL
     * 解除条件: 流量 > AL (跟随下限报警解除) */
    if (alarm_states[ALARM_TYPE_AAL] == ALARM_STATE_NORMAL) {
        if (instant_flow_m3h < aal) {
            alarm_states[ALARM_TYPE_AAL] = ALARM_STATE_ACTIVE;
            relay_set(ALARM_TYPE_AAL, 1);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AAL ACTIVE flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    } else {
        /* 下下限报警解除条件：流量升到下限报警值以上 */
        if (instant_flow_m3h > al) {
            alarm_states[ALARM_TYPE_AAL] = ALARM_STATE_NORMAL;
            relay_set(ALARM_TYPE_AAL, 0);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AAL NORMAL flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    }
}

/**
 * @brief  获取指定报警类型的当前状态
 * @param  type: 报警类型
 * @retval 报警状态 (ALARM_STATE_NORMAL/ALARM_STATE_ACTIVE)
 */
alarm_state_t app_alarm_get_state(alarm_type_t type)
{
    if (type >= ALARM_TYPE_COUNT) {
        return ALARM_STATE_NORMAL;
    }
    return alarm_states[type];
}

/**
 * @brief  获取所有继电器状态 (用于Modbus)
 * @param  relay_states: 4字节数组，存储继电器状态 (0=OFF, 1=ON)
 */
void app_alarm_get_relay_states(uint8_t relay_states[4])
{
    for (uint8_t i = 0; i < 4; i++) {
        /* 直接从GPIO读取实际状态 */
        GPIO_PinState state = HAL_GPIO_ReadPin(relay_gpio_map[i].port,
                                                relay_gpio_map[i].pin);
        relay_states[i] = (state == GPIO_PIN_SET) ? 1 : 0;
    }
}

/**
 * @brief  手动设置继电器状态 (用于测试/调试)
 * @param  relay_index: 继电器索引 (0-3)
 * @param  state: 状态 (0=OFF, 1=ON)
 */
void app_alarm_set_relay(uint8_t relay_index, uint8_t state)
{
    if (relay_index >= 4) {
        return;
    }
    relay_set(relay_index, state);
    /* 同步更新状态表 */
    alarm_states[relay_index] = (state) ? ALARM_STATE_ACTIVE : ALARM_STATE_NORMAL;
}
