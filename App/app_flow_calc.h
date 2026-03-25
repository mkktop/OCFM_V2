#ifndef __APP_FLOW_CALC_H__
#define __APP_FLOW_CALC_H__

#include "app_sensor.h"
#include "app_config.h"
#include "global.h"
#include <math.h>

/**
 * @brief 巴歇尔水槽参数结构体
 * @details 基于ISO 4359标准，存储不同规格巴歇尔水槽的计算参数
 *          公式: Q = K * H^n
 *          其中: Q-流量(L/s), H-水位(m), K-流量系数, n-指数
 *
 * @note  表格索引对应水槽规格:
 *       1: 1英寸    2: 2英寸    3: 3英寸    4: 6英寸
 *       5: 9英寸    6: 12英寸(0.25m)  7: 12英寸(0.30m)  8: 18英寸
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

/**
 * @brief  巴歇尔水槽参数表 (全局)
 * @note   索引0-7对应规格1-8
 */
extern Water_Channel g_channel_tbl[8];
/**
 * @brief  计算瞬时流量 (统一入口)
 * @param  water_level_m: 水位 (米)
 * @retval 瞬时流量 (根据配置的单位)
 *
 * @note   自动选择堰槽类型:
 *       - PARSHALL_FLUME: 巴歇尔槽
 *       - TRIANGULAR_WEIR: 三角堰
 *       - RECTANGULAR_WEIR: 矩形堰
 */
float flow_calc_instant(float water_level_m);

/**
 * @brief  计算巴歇尔槽瞬时流量
 * @param  water_level_m: 水位 (米)
 * @param  channel: 水槽参数指针
 * @retval 瞬时流量 (L/s)
 */
float parshall_flow_Ls(float water_level_m, Water_Channel *channel);

/**
 * @brief  计算三角堰瞬时流量 (TODO)
 * @param  water_level_m: 水位 (米)
 * @retval 瞬时流量 (L/s)
 * @note   公式: Q = 1.34 * h^(5/2) (90°三角堰)
 */
float triangular_weir_flow_Ls(float water_level_m);

/**
 * @brief  计算矩形堰瞬时流量 (TODO)
 * @param  water_level_m: 水位 (米)
 * @retval 瞬时流量 (L/s)
 * @note   公式: Q = 1.84 * (b - 0.2h) * h^(3/2)
 */
float rectangular_weir_flow_Ls(float water_level_m);

/**
 * @brief  瞬时流量单位转换
 * @param  flow_l_s: 瞬时流量 (L/s)
 * @param  unit: 目标单位
 * @retval 转换后的瞬时流量值
 */
float flow_convert_instant(float flow_l_s, flow_unit_t unit);

/*============================================================================*/
/*                           更新与获取接口                                     */
/*============================================================================*/

/**
 * @brief  更新流量计算 (每秒调用)
 * @note   从传感器获取水位，计算瞬时流量并累加累计流量
 */
void flow_calc_update(void);

/**
 * @brief  获取当前瞬时流量
 * @retval 瞬时流量 (L/s)
 */
float flow_calc_get_instant(void);

/**
 * @brief  获取累计流量
 * @retval 累计流量 (m³)
 */
double flow_calc_get_total(void);

/**
 * @brief  清零累计流量
 */
void flow_calc_reset_total(void);

#endif /* __APP_FLOW_CALC_H__ */
