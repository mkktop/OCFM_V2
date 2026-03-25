/**
 * @file    app_flow_calc.c
 * @brief   流量计算模块实现
 * @details  实现明渠流量计的核心计算功能
 *          - 瞬时流量计算(巴歇尔槽、三角堰、矩形堰)
 *          - 累计流量计算
 *          - 单位换算
 *
 * @note    水位数据来源: app_sensor 模块
 *          配置数据来源: app_config 模块
 */

#include "app_flow_calc.h"
#include "app_sensor.h"

/*============================================================================*/
/*                           私有变量                                          */
/*============================================================================*/

static float s_instant_flow_Ls = 0.0f;    /**< 当前瞬时流量 (L/s) */
static double s_total_flow_m3 = 0.0;     /**< 累计流量 (m³) */

/*============================================================================*/
/*                           参数表                                            */
/*============================================================================*/

/**
 * @brief 巴歇尔水槽参数表
 * @details 基于ISO 4359标准，存储不同规格巴歇尔水槽的计算参数
 *         公式: Q = K * H^n
 *         其中: Q-流量(L/s), H-水位(m), K-流量系数, n-指数
 *
 * @note  表格索引对应水槽规格:
 *       1: 1英寸    2: 2英寸    3: 3英寸    4: 6英寸
 *       5: 9英寸    6: 12英寸(0.25m)  7: 12英寸(0.30m)  8: 18英寸
 *
 * 字段说明: {规格编号, 喉道宽度(m), 流量系数K, 指数n, 最小水位(m), 最大水位(m), 最小流量(L/s), 最大流量(L/s)}
 */
Water_Channel g_channel_tbl[8] = {
    { 1, 0.025 ,   60.4 , 1.550 , 0.015 , 0.21 ,  0.09 ,    5.4 },   /* 1英寸  */
    { 2, 0.051 ,  120.7 , 1.550 , 0.015 , 0.24 ,  0.18 ,   13.2 },   /* 2英寸  */
    { 3, 0.076 ,  177.1 , 1.550 , 0.030 , 0.33 ,  0.77 ,   32.1 },   /* 3英寸  */
    { 4, 0.152 ,  381.2 , 1.540 , 0.030 , 0.45 ,  1.50 ,  111.0 },  /* 6英寸  */
    { 5, 0.228 ,  535.4 , 1.530 , 0.030 , 0.60 ,  2.50 ,    251 },   /* 9英寸  */
    { 6, 0.250 ,  561.0 , 1.513 , 0.030 , 0.60 ,  3.00 ,    250 },   /* 12英寸(0.25m) */
    { 7, 0.300 ,  679.0 , 1.521 , 0.030 , 0.75 ,  3.50 ,    400 },   /* 12英寸(0.30m) */
    { 8, 0.450 , 1038.0 , 1.537 , 0.030 , 0.75 ,  4.50 ,    630 },   /* 18英寸 */
};

/**
 * @brief  计算瞬时流量 (统一入口)
 * @param  water_level_m: 水位 (米)
 * @retval 瞬时流量 (根据配置的单位)
 *
 * @note   根据配置的水渠类型自动选择计算公式:
 *       - PARSHALL_FLUME: 巴歇尔槽
 *       - TRIANGULAR_WEIR: 三角堰 (TODO)
 *       - RECTANGULAR_WEIR: 矩形堰 (TODO)
 */
float flow_calc_instant(float water_level_m)
{
    float Q_l_s = 0.0f;  /* 基准单位: L/s */

    /* 根据水渠类型选择计算公式 */
    switch (app_config_get_canals_type()) {
        case PARSHALL_FLUME:  /* 巴歇尔槽 */
            Q_l_s = parshall_flow_Ls(water_level_m, &g_channel_tbl[app_config_get_channel_id() - 1]);
            break;

        case TRIANGULAR_WEIR:  /* 三角堰 */
            /* TODO: 实现三角堰流量计算 */
            // Q_l_s = triangular_weir_flow_Ls(water_level_m, ...);
            Q_l_s = 0.0f;
            break;

        case RECTANGULAR_WEIR:  /* 矩形堰 */
            /* TODO: 实现矩形堰流量计算 */
            // Q_l_s = rectangular_weir_flow_Ls(water_level_m, ...);
            Q_l_s = 0.0f;
            break;

        default:
            Q_l_s = 0.0f;
            break;
    }

    /* 单位转换 */
    if (app_config_get_instant_unit() != FLOW_UNIT_L_S) {
        return flow_convert_instant(Q_l_s, app_config_get_instant_unit());
    }
    return Q_l_s;
}

/**
 * @brief  计算巴歇尔槽瞬时流量
 * @param  water_level_m: 水位 (米)
 * @param  channel: 水槽参数指针
 * @retval 瞬时流量 (L/s)，水位无效时返回0
 *
 * @note   公式: Q = K * H^n
 *         超量程时水位会被限制在最大值
 */
float parshall_flow_Ls(float water_level_m, Water_Channel *channel)
{
    float Q = 0.0f;

    /* 检查空指针 */
    if (channel == NULL) {
        return 0.0f;
    }

    /* 水位在有效范围内 */
    if (water_level_m >= channel->water_level_down && water_level_m <= channel->water_level_up) {
        Q = channel->factor * powf(water_level_m, channel->n);
    }
    /* 水位低于下限 */
    else if (water_level_m < channel->water_level_down) {
        Q = 0.0f;
    }
    /* 水位高于上限 */
    else if (water_level_m > channel->water_level_up) {
        /* 限制水位在最大值 */
        water_level_m = channel->water_level_up;
        Q = channel->factor * powf(water_level_m, channel->n);
    }

    return Q;
}

/**
 * @brief  瞬时流量单位转换
 * @param  flow_l_s: 瞬时流量 (L/s)
 * @param  unit: 目标单位
 * @retval 转换后的瞬时流量值
 */
float flow_convert_instant(float flow_l_s, flow_unit_t unit)
{
    switch (unit) {
        case FLOW_UNIT_L_S:
            return flow_l_s;
        case FLOW_UNIT_L_MIN:
            return flow_l_s * 60.0f;
        case FLOW_UNIT_L_H:
            return flow_l_s * 3600.0f;
        case FLOW_UNIT_M3_H:
            return flow_l_s * 3.6f;  /* L/s → m³/h: 1000L=1m³, 3600s=1h */
        case FLOW_UNIT_M3_S:
            return flow_l_s / 1000.0f;
        case FLOW_UNIT_M3_MIN:
            return flow_l_s / 1000.0f * 60.0f;
        case FLOW_UNIT_T_H:
            return flow_l_s * 3.6f;  /* 假设水密度 1kg/L */
        case FLOW_UNIT_G_H:
            return flow_l_s * 3.785411784f;  /* 1L = 0.264172美制加仑 */
        default:
            return flow_l_s;
    }
}

/**
 * @brief  计算三角堰瞬时流量
 * @param  water_level_m: 水位 (米)
 * @retval 瞬时流量 (L/s)
 *
 * @note   TODO: 待实现
 *       公式: Q = 1.34 * h^(5/2) (90°三角堰)
 *       或: Q = C * h^(5/2), C根据堰口角度变化
 */
float triangular_weir_flow_Ls(float water_level_m)
{
    /* TODO: 实现三角堰流量计算 */
    (void)water_level_m;
    return 0.0f;
}

/**
 * @brief  计算矩形堰瞬时流量
 * @param  water_level_m: 水位 (米)
 * @retval 瞬时流量 (L/s)
 *
 * @note   TODO: 待实现
 *       公式: Q = 1.84 * (b - 0.2h) * h^(3/2)
 *       其中 b 为堰宽 (m)
 */
float rectangular_weir_flow_Ls(float water_level_m)
{
    /* TODO: 实现矩形堰流量计算 */
    (void)water_level_m;
    return 0.0f;
}

/*============================================================================*/
/*                           更新与获取接口                                     */
/*============================================================================*/

/**
 * @brief  更新流量计算 (每秒调用一次)
 * @details 从传感器获取水位，计算瞬时流量并累加累计流量
 *
 * @note   调用时机: 在 LVGL 定时器或 FreeRTOS 任务中每秒调用一次
 */
void flow_calc_update(void)
{
    SensorData_t *sensor;
    float water_level_m;

    /* 获取传感器数据 */
    sensor = app_sensor_get_data();
    if (sensor == NULL || !sensor->is_online) {
        s_instant_flow_Ls = 0.0f;
        return;
    }

    water_level_m = sensor->water_level_m;

    /* 计算瞬时流量 (L/s) */
    s_instant_flow_Ls = flow_calc_instant(water_level_m);

    /* 累加累计流量: L/s * 1s = L, 除以1000转m³ */
    s_total_flow_m3 += s_instant_flow_Ls / 1000.0;
}

/**
 * @brief  获取当前瞬时流量
 * @retval 瞬时流量 (根据配置的单位)
 */
float flow_calc_get_instant(void)
{
    return s_instant_flow_Ls;
}

/**
 * @brief  获取累计流量
 * @retval 累计流量 (m³)
 */
double flow_calc_get_total(void)
{
    return s_total_flow_m3;
}

/**
 * @brief  清零累计流量
 */
void flow_calc_reset_total(void)
{
    s_total_flow_m3 = 0.0;
}
