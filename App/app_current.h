/**
 * @file    app_current.h
 * @brief   4-20mA输出电流模块
 * @details 将瞬时流量线性映射到 PWM 占空比，驱动外部 V/I 电路输出 4~20mA
 *
 *          硬件链路: TIM3 CH4 (PB1) → RC低通滤波 → V/I转换 → 4~20mA输出
 *
 *          校准参数:
 *          - calibration_4ma/20ma: CCR硬件校准值 (出厂标定)
 *          - range_4ma/20ma: 流量量程端点 (用户可调)
 */

#ifndef __APP_CURRENT_H__
#define __APP_CURRENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  初始化4-20mA输出
 * @note   启动PWM，设置初始CCR为4mA，在系统启动时调用一次
 */
void app_current_init(void);

/**
 * @brief  更新4-20mA PWM输出
 * @param  flow_data: 当前瞬时流量 (m³/h)
 * @note   建议每秒调用一次 (如放在 flow_refresh_fun 定时器回调中)
 */
void app_current_update(float flow_data);

/**
 * @brief  计算4-20mA显示电流值 (纯计算，不驱动硬件)
 * @param  flow_data: 当前瞬时流量 (m³/h)
 * @retval 电流值×100（整数），如 1200 = 12.00mA
 */
uint32_t app_current_calc_ma(float flow_data);

/**
 * @brief  计算4-20mA显示电流值并格式化为字符串
 * @param  flow_data: 当前瞬时流量 (m³/h)
 * @param  buf:       输出缓冲区（建议至少16字节）
 * @param  buf_size:  缓冲区大小
 * @note   输出格式: "12.00mA"
 */
void app_current_format_ma(float flow_data, char *buf, uint32_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CURRENT_H__ */
