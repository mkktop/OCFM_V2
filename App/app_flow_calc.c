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
/*                           私有类型                                          */
/*============================================================================*/

/**
 * @brief 巴歇尔水槽参数结构体
 * @details 基于ISO 4359标准
 *          计算公式: Q = K * H^n
 *          其中: Q-流量(L/s), H-水位(m), K-流量系数, n-指数
 */
typedef struct {
    uint32_t number_ID;        /**< 水槽规格编号 (1-8) */
    float width;               /**< 喉道宽度 (m) */
    float factor;              /**< 流量系数 K */
    float n;                   /**< 指数 n */
    float water_level_down;    /**< 最小水位限制 (m) */
    float water_level_up;      /**< 最大水位限制 (m) */
    float flow_range_down;     /**< 最小流量限制 (L/s) */
    float flow_range_up;       /**< 最大流量限制 (L/s) */
} Water_Channel;

/*============================================================================*/
/*                           私有变量                                          */
/*============================================================================*/

static float s_instant_flow = 0.0f;     /**< 当前瞬时流量 (根据配置的单位) */
static double s_total_flow_m3 = 0.0;    /**< 累计流量 (m³) */

/*============================================================================*/
/*                           私有数据                                          */
/*============================================================================*/

/**
 * @brief 巴歇尔水槽参数表 (ISO 4359)
 * @note  索引0-7对应规格1-8
 *        规格: 1英寸, 2英寸, 3英寸, 6英寸, 9英寸, 12英寸(0.25m), 12英寸(0.30m), 18英寸
 */
static const Water_Channel s_channel_tbl[8] = {
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
 * @brief  计算巴歇尔槽瞬时流量
 * @param  water_level_m: 水位 (米)
 * @param  channel: 水槽参数指针
 * @retval 瞬时流量 (L/s)，水位无效时返回0
 * @note   公式: Q = K * H^n，超量程时限制在最大值
 */
static float parshall_flow_Ls(float water_level_m, const Water_Channel *channel)
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
static float flow_convert_instant(float flow_l_s, flow_unit_t unit)
{
    switch (unit) {
        case FLOW_UNIT_L_S:
            return flow_l_s;
        case FLOW_UNIT_L_MIN:
            return flow_l_s * 60.0f;
        case FLOW_UNIT_L_H:
            return flow_l_s * 3600.0f;
        case FLOW_UNIT_M3_H:
            return flow_l_s * 3.6f;
        case FLOW_UNIT_M3_S:
            return flow_l_s / 1000.0f;
        case FLOW_UNIT_M3_MIN:
            return flow_l_s / 1000.0f * 60.0f;
        case FLOW_UNIT_T_H:
            return flow_l_s * 3.6f;
        case FLOW_UNIT_G_H:
            return flow_l_s * 3.785411784f;
        default:
            return flow_l_s;
    }
}

/**
 * @brief  计算三角堰瞬时流量
 * @param  water_level_m: 水位 (米)
 * @retval 瞬时流量 (L/s)
 * @note   TODO: 待实现
 *         公式: Q = 1.34 * h^(5/2) (90°三角堰)
 */
static float triangular_weir_flow_Ls(float water_level_m)
{
    (void)water_level_m;
    return 0.0f;
}

/**
 * @brief  计算矩形堰瞬时流量
 * @param  water_level_m: 水位 (米)
 * @retval 瞬时流量 (L/s)
 * @note   TODO: 待实现
 *         公式: Q = 1.84 * (b - 0.2h) * h^(3/2)
 */
static float rectangular_weir_flow_Ls(float water_level_m)
{
    (void)water_level_m;
    return 0.0f;
}

/**
 * @brief  计算瞬时流量 (统一入口)
 * @param  water_level_m: 水位 (米)
 * @retval 瞬时流量 (L/s) - 固定返回基准单位
 */
static float flow_calc_instant(float water_level_m)
{
    float Q_l_s = 0.0f;

    switch (app_config_get_canals_type()) {
        case PARSHALL_FLUME:
            Q_l_s = parshall_flow_Ls(water_level_m, &s_channel_tbl[app_config_get_channel_id() - 1]);
            break;
        case TRIANGULAR_WEIR:
            Q_l_s = triangular_weir_flow_Ls(water_level_m);
            break;
        case RECTANGULAR_WEIR:
            Q_l_s = rectangular_weir_flow_Ls(water_level_m);
            break;
        default:
            Q_l_s = 0.0f;
            break;
    }
    return Q_l_s;
}

/*============================================================================*/
/*                           对外接口                                           */
/*============================================================================*/

/**
 * @brief  更新流量计算 (每秒调用)
 * @note   从传感器获取水位，计算瞬时流量并累加累计流量
 *         - 瞬时流量: 根据配置的单位显示
 *         - 累计流量: 固定使用 m³
 */
void flow_calc_update(void)
{
    SensorData_t *sensor;
    float water_level_m;

    /* 获取传感器数据 */
    sensor = app_sensor_get_data();
    if (sensor == NULL || !sensor->is_online) {
        s_instant_flow = 0.0f;
        return;
    }

    water_level_m = sensor->water_level_m;

    /* 计算瞬时流量 (L/s) */
    s_instant_flow = flow_calc_instant(water_level_m);

    /* 累加累计流量: L/s * 1s = L, 除以1000转m³ */
    s_total_flow_m3 += s_instant_flow / 1000.0f;

    /* 单位转换 */
    if (app_config_get_instant_unit() != FLOW_UNIT_L_S) {
        s_instant_flow = flow_convert_instant(s_instant_flow, app_config_get_instant_unit());
    }
}

/**
 * @brief  获取当前瞬时流量
 * @retval 瞬时流量 (根据配置的单位)
 */
float flow_calc_get_instant(void)
{
    return s_instant_flow;
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
