/**
 * @file    app_alarm.c
 * @brief   报警模块实现 - 瞬时流量报警控制继电器输出
 * @details 根据瞬时流量(m³/h)控制四路继电器输出
 *          滞回逻辑，防止频繁触发/恢复
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
 * @brief 报警状态表 (当前报警是否激活)
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
 * @brief  控制继电器动作
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
/*                           公共接口                                           */
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
 * @param  instant_flow_m3h: 瞬时流量 (m³/h)
 * @param  sensor_online: 传感器是否在线 (0=离线, 1=在线)
 * @note   报警逻辑说明
 *         上限报警(AH)：触发>=AH，恢复<AH-DH (回差)
 *         下限报警(AL)：触发<=AL，恢复>AL+DL (回差)
 *         上上限报警(AAH)：触发>=AAH，恢复<AH (无回差)
 *         下下限报警(AAL)：触发<=AAL，恢复>AL (无回差)
 *         传感器离线时解除所有报警
 */
void app_alarm_update(float instant_flow_m3h, uint8_t sensor_online)
{
    float ah, al, dh, dl, aah, aal;

    /* 传感器离线时解除所有报警并返回 */
    if (!sensor_online) {
        for (uint8_t i = 0; i < ALARM_TYPE_COUNT; i++) {
            if (alarm_states[i] == ALARM_STATE_ACTIVE) {
                alarm_states[i] = ALARM_STATE_NORMAL;
                relay_set(i, 0);
            }
        }
        return;
    }

    /* 获取报警设定值 (单位: m³/h) */
    ah = app_config_get_alarm_ah();
    al = app_config_get_alarm_al();
    dh = app_config_get_alarm_dh();
    dl = app_config_get_alarm_dl();
    aah = app_config_get_alarm_aah();
    aal = app_config_get_alarm_aal();

    /* ========== 上限报警 (继电器1) ========== */
    /* 触发条件: 流量 >= AH
     * 恢复条件: 流量 < AH - DH (回差) */
    if (alarm_states[ALARM_TYPE_AH] == ALARM_STATE_NORMAL) {
        /* 正常状态，检查是否需要触发 */
        if (instant_flow_m3h >= ah) {
            alarm_states[ALARM_TYPE_AH] = ALARM_STATE_ACTIVE;
            relay_set(ALARM_TYPE_AH, 1);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AH ACTIVE flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    } else {
        /* 报警状态，检查是否需要恢复 */
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
    /* 触发条件: 流量 >= AAH
     * 恢复条件: 流量 < AH (无独立回差) */
    if (alarm_states[ALARM_TYPE_AAH] == ALARM_STATE_NORMAL) {
        if (instant_flow_m3h >= aah) {
            alarm_states[ALARM_TYPE_AAH] = ALARM_STATE_ACTIVE;
            relay_set(ALARM_TYPE_AAH, 1);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AAH ACTIVE flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    } else {
        /* 上上限报警恢复依赖于上限报警阈值 */
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
    /* 触发条件: 流量 <= AL
     * 恢复条件: 流量 > AL + DL (回差) */
    if (alarm_states[ALARM_TYPE_AL] == ALARM_STATE_NORMAL) {
        /* 正常状态，检查是否需要触发 */
        if (instant_flow_m3h <= al) {
            alarm_states[ALARM_TYPE_AL] = ALARM_STATE_ACTIVE;
            relay_set(ALARM_TYPE_AL, 1);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AL ACTIVE flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    } else {
        /* 报警状态，检查是否需要恢复 */
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
    /* 触发条件: 流量 <= AAL
     * 恢复条件: 流量 > AL (无独立回差) */
    if (alarm_states[ALARM_TYPE_AAL] == ALARM_STATE_NORMAL) {
        if (instant_flow_m3h <= aal) {
            alarm_states[ALARM_TYPE_AAL] = ALARM_STATE_ACTIVE;
            relay_set(ALARM_TYPE_AAL, 1);
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "AAL ACTIVE flow=%.2f", instant_flow_m3h);
                app_log_send(LOG_TYPE_ALARM, buf);
            }
        }
    } else {
        /* 下下限报警恢复依赖于下限报警阈值 */
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
 * @brief  获取所有继电器的状态 (用于Modbus)
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
 * @brief  手动设置继电器状态 (用于调试/测试)
 * @param  relay_index: 继电器索引 (0-3)
 * @param  state: 状态 (0=OFF, 1=ON)
 */
void app_alarm_set_relay(uint8_t relay_index, uint8_t state)
{
    if (relay_index >= 4) {
        return;
    }
    relay_set(relay_index, state);
    /* 同步报警状态表 */
    alarm_states[relay_index] = (state) ? ALARM_STATE_ACTIVE : ALARM_STATE_NORMAL;
}
