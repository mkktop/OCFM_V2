/**
 * @file    app_current.h
 * @brief   4-20mA输出电流计算模块
 * @details 将瞬时流量（工程量）线性映射到 4.00mA ~ 20.00mA 输出电流值
 *          使用 calibration_4ma / calibration_20ma 作为量程端点
 */

#ifndef __APP_CURRENT_H__
#define __APP_CURRENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  计算4-20mA输出电流值
 * @param  flow_data: 当前瞬时流量（m³/h，与校准值单位一致）
 * @retval 电流值×100（整数），如 1200 = 12.00mA
 *
 * @note   公式: outma = (data - minlc) × 1600 / fenmu + 400
 *         范围: 400 ~ 2000（对应 4.00mA ~ 20.00mA）
 *         限幅: 400 ~ 2380（预留超量程显示空间）
 */
uint32_t app_current_calc_ma(float flow_data);

/**
 * @brief  计算4-20mA输出电流并格式化为字符串
 * @param  flow_data: 当前瞬时流量（m³/h）
 * @param  buf:       输出缓冲区（建议至少16字节）
 * @param  buf_size:  缓冲区大小
 * @retval 无
 *
 * @note   输出格式: "12.00mA"
 */
void app_current_format_ma(float flow_data, char *buf, uint32_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CURRENT_H__ */
