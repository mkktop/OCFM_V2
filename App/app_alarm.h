/**
 * @file    app_alarm.h
 * @brief   报警模块 - 瞬时流量报警控制继电器
 * @details 基于瞬时流量(m?/h)触发四路继电器报警
 *          - 上限报警(AH)   -> 继电器1
 *          - 下限报警(AL)   -> 继电器2
 *          - 上上限报警(AAH) -> 继电器3
 *          - 下下限报警(AAL) -> 继电器4
 *          报警带回差(hysteresis)防止频繁触发/解除
 */

#ifndef __APP_ALARM_H__
#define __APP_ALARM_H__

#include <stdint.h>

/*============================================================================*/
/*                           报警类型定义                                       */
/*============================================================================*/

/**
 * @brief 报警类型枚举
 */
typedef enum {
    ALARM_TYPE_AH   = 0,    /**< 上限报警 (继电器1) */
    ALARM_TYPE_AL   = 1,    /**< 下限报警 (继电器2) */
    ALARM_TYPE_AAH  = 2,    /**< 上上限报警 (继电器3) */
    ALARM_TYPE_AAL  = 3,    /**< 下下限报警 (继电器4) */
    ALARM_TYPE_COUNT = 4    /**< 报警类型数量 */
} alarm_type_t;

/**
 * @brief 报警状态枚举
 */
typedef enum {
    ALARM_STATE_NORMAL = 0,     /**< 正常状态 (继电器OFF) */
    ALARM_STATE_ACTIVE  = 1     /**< 报警状态 (继电器ON) */
} alarm_state_t;

/*============================================================================*/
/*                           对外接口                                           */
/*============================================================================*/

/**
 * @brief  初始化报警模块
 * @note   启动时调用，初始化继电器为关闭状态
 */
void app_alarm_init(void);

/**
 * @brief  更新报警判断 (每秒调用)
 * @param  instant_flow_m3h: 瞬时流量 (m?/h)
 * @note   根据瞬时流量和报警阈值判断是否触发报警
 *         回差逻辑：
 *         - 上限报警触发: flow > AH,  解除: flow < AH - DH
 *         - 上上限报警触发: flow > AAH (无回差)
 *         - 下限报警触发: flow < AL,  解除: flow > AL + DL
 *         - 下下限报警触发: flow < AAL (无回差)
 */
void app_alarm_update(float instant_flow_m3h);

/**
 * @brief  获取指定报警类型的当前状态
 * @param  type: 报警类型
 * @retval 报警状态 (ALARM_STATE_NORMAL/ALARM_STATE_ACTIVE)
 */
alarm_state_t app_alarm_get_state(alarm_type_t type);

/**
 * @brief  获取所有继电器状态 (用于Modbus)
 * @param  relay_states: 4字节数组，存储继电器状态 (0=OFF, 1=ON)
 */
void app_alarm_get_relay_states(uint8_t relay_states[4]);

/**
 * @brief  手动设置继电器状态 (用于测试/调试)
 * @param  relay_index: 继电器索引 (0-3)
 * @param  state: 状态 (0=OFF, 1=ON)
 */
void app_alarm_set_relay(uint8_t relay_index, uint8_t state);

#endif /* __APP_ALARM_H__ */
