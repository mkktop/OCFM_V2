/**
 * @file    app_current.c
 * @brief   4-20mA输出电流计算模块实现
 * @details 将瞬时流量线性映射到 4.00mA ~ 20.00mA 输出电流
 *
 *          映射公式（整数×100，避免浮点显示精度问题）:
 *          outma = (data - minlc) × 1600 / fenmu + 400
 *
 *          符号说明:
 *          - data:   当前瞬时流量（工程量）
 *          - minlc:  4mA对应的工程量（calibration_4ma 和 calibration_20ma 中较小值）
 *          - fenmu:  量程跨度 = |calibration_20ma - calibration_4ma|
 *          - 1600:   20.00mA - 4.00mA = 16.00mA，放大100倍
 *          - 400:    4.00mA，放大100倍
 */

#include "app_current.h"
#include "app_config.h"
#include <stdio.h>

/**
 * @brief  计算4-20mA输出电流值
 * @param  flow_data: 当前瞬时流量（工程量）
 * @retval 电流值×100（整数），如 1200 = 12.00mA
 */
uint32_t app_current_calc_ma(float flow_data)
{
    uint32_t cal_4ma  = (uint32_t)app_config_get_calibration_4ma();
    uint32_t cal_20ma = (uint32_t)app_config_get_calibration_20ma();
    uint32_t fenmu, minlc;
    float outpr;

    /* 确保分母为正 */
    if (cal_20ma > cal_4ma) {
        fenmu = cal_20ma - cal_4ma;
        minlc = cal_4ma;
    } else {
        fenmu = cal_4ma - cal_20ma;
        minlc = cal_20ma;
    }

    /* 防止除零 */
    if (fenmu == 0) {
        return 400;
    }

    outpr = (flow_data - minlc) * 1600.0f;
    outpr = outpr / (float)fenmu;

    uint32_t outma = (uint32_t)(outpr + 400.0f);

    /* 限幅 */
    if (outma > 2380)
        outma = 2380;
    else if (outma < 400)
        outma = 400;

    return outma;
}

/**
 * @brief  计算4-20mA输出电流并格式化为字符串
 * @param  flow_data: 当前瞬时流量（工程量）
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
