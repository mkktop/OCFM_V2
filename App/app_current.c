/**
 * @file    app_current.c
 * @brief   4-20mA输出电流模块
 * @details 将瞬时流量线性映射到 PWM 占空比，驱动外部 V/I 电路输出 4~20mA
 *
 *          硬件链路: TIM3 CH4 (PB1) → RC低通滤波 → V/I转换 → 4~20mA输出
 *          TIM3 配置: ARR=1999, OCPOLARITY_LOW
 *          CCR 越大 → 低电平占比越大 → 平均电压越低
 */

#include "app_current.h"
#include "app_config.h"
#include "tim.h"
#include <stdio.h>

/*============================================================================*/
/*                           私有宏定义                                        */
/*============================================================================*/

#define TIM_ARR    7999

/*============================================================================*/
/*                           私有变量                                          */
/*============================================================================*/

static uint8_t s_cal_mode = 0;       /**< 校准模式标志 (1=校准中, 跳过正常更新) */
static uint32_t s_cal_tick = 0;      /**< 校准模式进入时间戳 */

#define CAL_TIMEOUT_MS  10000        /**< 校准模式超时: 10秒无更新自动退出 */

/*============================================================================*/
/*                           公共函数实现                                      */
/*============================================================================*/

/**
 * @brief  初始化4-20mA输出
 * @note   启动PWM输出，设置初始CCR为4mA对应的值
 */
void app_current_init(void)
{
    uint32_t ccr_4ma = app_config_get_calibration_4ma();

    /* 启动PWM输出 */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

    /* 设置初始占空比为4mA */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, (uint32_t)ccr_4ma);
}

/**
 * @brief  更新4-20mA PWM输出
 * @param  flow_data: 当前瞬时流量 (m³/h)
 */
void app_current_update(float flow_data)
{
    /* 校准模式下跳过正常更新，PWM 由 app_current_set_calibration() 控制 */
    if (s_cal_mode) return;

    uint32_t ccr_4ma  = app_config_get_calibration_4ma();
    uint32_t ccr_20ma = app_config_get_calibration_20ma();
    float range_4ma  = app_config_get_range_4ma();
    float range_20ma = app_config_get_range_20ma();
    uint32_t ccr;
    float ratio;

    /* 防止除零 */
    if (range_20ma == range_4ma) {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, (uint32_t)ccr_4ma);
        return;
    }

    /* 流量线性映射到CCR */
    ratio = (flow_data - range_4ma) / (range_20ma - range_4ma);

    if (ratio <= 0.0f) {
        ccr = ccr_4ma;
    } else if (ratio >= 1.0f) {
        ccr = ccr_20ma;
    } else {
        ccr = (uint32_t)(ccr_4ma + ratio * (ccr_20ma - ccr_4ma));
    }

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, ccr);
}

/**
 * @brief  计算4-20mA显示电流值 (纯计算，不驱动硬件)
 * @param  flow_data: 当前瞬时流量 (m³/h)
 * @retval 电流值×100（整数），如 1200 = 12.00mA
 */
uint32_t app_current_calc_ma(float flow_data)
{
    float range_4ma  = app_config_get_range_4ma();
    float range_20ma = app_config_get_range_20ma();
    float ratio;

    if (range_20ma == range_4ma) {
        return 400;
    }

    ratio = (flow_data - range_4ma) / (range_20ma - range_4ma);

    if (ratio <= 0.0f) {
        return 400;
    } else if (ratio >= 1.0f) {
        return 2000;
    } else {
        return (uint32_t)(400 + ratio * 1600);
    }
}

/**
 * @brief  计算4-20mA显示电流值并格式化为字符串
 * @param  flow_data: 当前瞬时流量 (m³/h)
 * @param  buf:       输出缓冲区
 * @param  buf_size:  缓冲区大小
 */
void app_current_format_ma(float flow_data, char *buf, uint32_t buf_size)
{
    uint32_t outma = app_current_calc_ma(flow_data);
    snprintf(buf, buf_size, "%lu.%02lumA",
             (unsigned long)(outma / 100),
             (unsigned long)(outma % 100));
}

/**
 * @brief  进入/更新校准模式
 * @param  ccr_value: 直接输出到PWM的CCR值
 * @note   在设置页面编辑校准值时调用，PWM直接跟随编辑值输出
 *         正常的 app_current_update() 在校准模式下被跳过
 */
void app_current_set_calibration(uint32_t ccr_value)
{
    s_cal_mode = 1;
    s_cal_tick = HAL_GetTick();
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, ccr_value);
}

/**
 * @brief  退出校准模式，恢复正常PWM控制
 * @note   在退出校准编辑页或超时时调用
 */
void app_current_exit_calibration(void)
{
    s_cal_mode = 0;
    s_cal_tick = 0;
}

/**
 * @brief  校准模式超时检测，需周期调用
 * @note   10秒无新的校准写入则自动退出
 */
void app_current_calibration_tick(void)
{
    if (s_cal_mode && s_cal_tick && (HAL_GetTick() - s_cal_tick >= CAL_TIMEOUT_MS)) {
        app_current_exit_calibration();
    }
}
