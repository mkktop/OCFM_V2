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
#include "rtc.h"
#include "at24c02.h"
#include <string.h>

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

/**
 * @brief 三角堰参数结构体
 * @details 基于ISO 1438标准
 *          计算公式: Q = K * H^n
 *          其中: Q-流量(L/s), H-水位(m), K-流量系数, n=2.5
 *          常用角度: 90°, 60°, 45°, 30°
 */
typedef struct {
    uint32_t number_ID;        /**< 堰型编号 (1-4) */
    float angle_deg;           /**< 堰口角度 (度) */
    float factor;              /**< 流量系数 K */
    float n;                   /**< 指数 n (固定2.5) */
    float water_level_down;    /**< 最小水位限制 (m) */
    float water_level_up;      /**< 最大水位限制 (m) */
    float flow_range_down;     /**< 最小流量限制 (L/s) */
    float flow_range_up;       /**< 最大流量限制 (L/s) */
} TriangularWeir_t;

/**
 * @brief 矩形堰参数结构体
 * @details 基于ISO 1438标准
 *          计算公式: Q = K * b * H^1.5
 *          其中: Q-流量(L/s), H-水位(m), K-流量系数, b-堰宽(m)
 *          常用堰宽: 0.5m, 1.0m, 1.5m, 2.0m
 */
typedef struct {
    uint32_t number_ID;        /**< 堰型编号 (1-4) */
    float width;               /**< 堰宽 (m) */
    float factor;              /**< 流量系数 K (约1.84) */
    float n;                   /**< 指数 n (固定1.5) */
    float water_level_down;    /**< 最小水位限制 (m) */
    float water_level_up;      /**< 最大水位限制 (m) */
    float flow_range_down;     /**< 最小流量限制 (L/s) */
    float flow_range_up;       /**< 最大流量限制 (L/s) */
} RectangularWeir_t;

/**
 * @brief 累计流量EEPROM存储结构体
 */
typedef struct {
    uint32_t magic;            /**< 校验标志 */
    double total_flow;         /**< 累计流量 (m³) */
    uint32_t total_time;       /**< 累计时长 (秒) */
    uint32_t reserved;         /**< 保留字段 */
} TotalFlowStorage_t;

/*============================================================================*/
/*                           私有变量                                          */
/*============================================================================*/

static float s_instant_flow = 0.0f;     /**< 当前瞬时流量 (根据配置的单位) */
static double s_total_flow_m3 = 0.0;    /**< 累计流量 (m³) */
static uint32_t s_total_time_sec = 0;   /**< 累计时长 (秒) */
static uint8_t s_bkp_save_counter = 0;  /**< 备份寄存器保存计数器 (10秒周期) */
static uint16_t s_eeprom_save_counter = 0; /**< EEPROM保存计数器 (5分钟周期) */
static volatile uint8_t s_eeprom_save_pending = 0; /**< EEPROM待保存标志 */

/*============================================================================*/
/*                           私有数据                                          */
/*============================================================================*/

/**
 * @brief 巴歇尔水槽参数表 (ISO 4359)
 * @note  索引0-15对应规格1-16
 *        公式: Q = K * H^n (L/s, m)
 */
static const Water_Channel s_channel_tbl[16] = {
    {  1, 0.025,   60.4, 1.550, 0.015, 0.21,   0.09,     5.4 },   /* 0.025m  */
    {  2, 0.051,  120.7, 1.550, 0.015, 0.24,   0.18,    13.2 },   /* 0.051m  */
    {  3, 0.076,  177.1, 1.550, 0.030, 0.33,   0.77,    32.1 },   /* 0.076m  */
    {  4, 0.152,  381.2, 1.540, 0.030, 0.45,   1.50,   111.0 },   /* 0.152m  */
    {  5, 0.228,  535.4, 1.530, 0.030, 0.60,   2.50,   251.0 },   /* 0.228m  */
    {  6, 0.250,  561.0, 1.513, 0.030, 0.60,   3.00,   250.0 },   /* 0.250m  */
    {  7, 0.300,  679.0, 1.521, 0.030, 0.75,   3.50,   400.0 },   /* 0.300m  */
    {  8, 0.450, 1038.0, 1.537, 0.030, 0.75,   4.50,   630.0 },   /* 0.450m  */
    {  9, 0.600, 1403.0, 1.548, 0.050, 0.75,  12.50,   850.0 },   /* 0.60m   */
    { 10, 0.750, 1772.0, 1.557, 0.060, 0.75,  25.00,  1100.0 },   /* 0.75m   */
    { 11, 0.900, 2147.0, 1.565, 0.060, 0.75,  30.00,  1250.0 },   /* 0.90m   */
    { 12, 1.000, 2397.0, 1.569, 0.060, 0.80,  30.00,  1500.0 },   /* 1.00m   */
    { 13, 1.200, 2904.0, 1.577, 0.060, 0.80,  35.00,  2000.0 },   /* 1.20m   */
    { 14, 1.500, 3668.0, 1.586, 0.060, 0.80,  45.00,  2500.0 },   /* 1.50m   */
    { 15, 1.800, 4440.0, 1.593, 0.080, 0.80,  80.00,  3000.0 },   /* 1.80m   */
    { 16, 2.100, 5222.0, 1.599, 0.080, 0.80,  95.00,  3600.0 },   /* 2.10m   */
};

/**
 * @brief 三角堰参数表 (ISO 1438)
 * @note  索引0-4对应规格1-5
 *        规格: 90°三角堰, 60°三角堰, 45°三角堰, 30°三角堰, 22.5°三角堰
 *        公式: Q = K * H^2.5 (L/s, m)
 *        K系数已转换为L/s单位 (原m³/s系数 × 1000)
 */
static const TriangularWeir_t s_triangular_weir_tbl[5] = {
    { 1,  90.0f, 1340.0f, 2.50f, 0.05f, 0.40f,  0.75f,  136.0f },  /* 90°三角堰 */
    { 2,  60.0f,  770.0f, 2.50f, 0.05f, 0.35f,  0.43f,  55.8f },   /* 60°三角堰 */
    { 3,  45.0f,  560.0f, 2.50f, 0.05f, 0.30f,  0.31f,  27.6f },   /* 45°三角堰 */
    { 4,  30.0f,  370.0f, 2.50f, 0.05f, 0.25f,  0.21f,  11.6f },   /* 30°三角堰 */
    { 5,  22.5f,  300.0f, 2.50f, 0.05f, 0.25f,  0.17f,   9.4f },   /* 22.5°三角堰 */
};

/**
 * @brief 矩形堰参数表 (ISO 1438)
 * @note  索引0-3对应规格1-4
 *        规格: 0.5m堰宽, 1.0m堰宽, 1.5m堰宽, 2.0m堰宽
 *        公式: Q = K * b * H^1.5 (L/s, m) - 无侧收缩（全宽堰）
 *        K系数已转换为L/s单位 (原m³/s系数 × 1000)
 */
static const RectangularWeir_t s_rectangular_weir_tbl[4] = {
    { 1, 0.50f, 1840.0f, 1.50f, 0.05f, 0.50f,   10.0f,  330.0f },   /* 0.5m堰宽 */
    { 2, 1.00f, 1840.0f, 1.50f, 0.05f, 0.60f,   20.0f,  860.0f },   /* 1.0m堰宽 */
    { 3, 1.50f, 1840.0f, 1.50f, 0.05f, 0.70f,   30.0f, 1530.0f },   /* 1.5m堰宽 */
    { 4, 2.00f, 1840.0f, 1.50f, 0.05f, 0.80f,   40.0f, 2640.0f },   /* 2.0m堰宽 */
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
 * @param  weir: 三角堰参数指针
 * @retval 瞬时流量 (L/s)，水位无效时返回0
 * @note   公式: Q = K * H^n，超量程时限制在最大值
 */
static float triangular_weir_flow_Ls(float water_level_m, const TriangularWeir_t *weir)
{
    float Q = 0.0f;

    /* 检查空指针 */
    if (weir == NULL) {
        return 0.0f;
    }

    /* 水位在有效范围内 */
    if (water_level_m >= weir->water_level_down && water_level_m <= weir->water_level_up) {
        Q = weir->factor * powf(water_level_m, weir->n);
    }
    /* 水位低于下限 */
    else if (water_level_m < weir->water_level_down) {
        Q = 0.0f;
    }
    /* 水位高于上限 */
    else if (water_level_m > weir->water_level_up) {
        /* 限制水位在最大值 */
        water_level_m = weir->water_level_up;
        Q = weir->factor * powf(water_level_m, weir->n);
    }

    return Q;
}

/**
 * @brief  计算矩形堰瞬时流量
 * @param  water_level_m: 水位 (米)
 * @param  weir: 矩形堰参数指针
 * @retval 瞬时流量 (L/s)，水位无效时返回0
 * @note   公式: Q = K * b * H^n (无侧收缩/全宽堰)
 *         超量程时限制在最大值
 */
static float rectangular_weir_flow_Ls(float water_level_m, const RectangularWeir_t *weir)
{
    float Q = 0.0f;

    /* 检查空指针 */
    if (weir == NULL) {
        return 0.0f;
    }

    /* 水位在有效范围内 */
    if (water_level_m >= weir->water_level_down && water_level_m <= weir->water_level_up) {
        Q = weir->factor * weir->width * powf(water_level_m, weir->n);
    }
    /* 水位低于下限 */
    else if (water_level_m < weir->water_level_down) {
        Q = 0.0f;
    }
    /* 水位高于上限 */
    else if (water_level_m > weir->water_level_up) {
        /* 限制水位在最大值 */
        water_level_m = weir->water_level_up;
        Q = weir->factor * weir->width * powf(water_level_m, weir->n);
    }

    return Q;
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
            Q_l_s = triangular_weir_flow_Ls(water_level_m, &s_triangular_weir_tbl[app_config_get_channel_id() - 1]);
            break;
        case RECTANGULAR_WEIR:
            Q_l_s = rectangular_weir_flow_Ls(water_level_m, &s_rectangular_weir_tbl[app_config_get_channel_id() - 1]);
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
 * @brief  保存累计流量和累计时长到备份寄存器
 * @note   DR1/DR2: 累计流量(double), DR3: magic
 *         DR4: 累计时长(uint32)
 */
void flow_calc_save_total(void)
{
    uint32_t buf[2];
    extern RTC_HandleTypeDef hrtc;

    /* 保存累计流量 */
    memcpy(buf, &s_total_flow_m3, sizeof(double));
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, buf[0]);
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, buf[1]);
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3, TOTAL_FLOW_MAGIC_NUMBER);

    /* 保存累计时长 */
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR4, s_total_time_sec);
}

/**
 * @brief  从备份寄存器加载累计流量和累计时长
 * @retval 1: 加载成功, 0: 数据无效
 */
static uint8_t flow_calc_load_from_backup(void)
{
    uint32_t buf[2];
    uint32_t magic;
    double total_flow = 0.0;
    uint32_t total_time = 0;
    extern RTC_HandleTypeDef hrtc;

    /* 校验magic number */
    magic = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3);
    if (magic != TOTAL_FLOW_MAGIC_NUMBER) {
        return 0;
    }

    buf[0] = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);
    buf[1] = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);
    memcpy(&total_flow, buf, sizeof(double));

    /* 检查是否为有效数据 (NaN检查) */
    if (total_flow < 0.0 || total_flow >= 1e12) {
        return 0;
    }

    /* 读取累计时长 */
    total_time = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR4);

    s_total_flow_m3 = total_flow;
    s_total_time_sec = total_time;
    return 1;
}

/**
 * @brief  保存累计流量和累计时长到EEPROM
 * @retval 1: 成功, 0: 失败
 */
static uint8_t flow_calc_save_to_eeprom(void)
{
    TotalFlowStorage_t storage;

    storage.magic = TOTAL_FLOW_MAGIC_NUMBER;
    storage.total_flow = s_total_flow_m3;
    storage.total_time = s_total_time_sec;
    storage.reserved = 0;

    return at24c02_write_buffer(TOTAL_FLOW_EEPROM_ADDR, sizeof(TotalFlowStorage_t),
                                (uint8_t*)&storage);
}

/**
 * @brief  从EEPROM加载累计流量和累计时长
 * @retval 1: 加载成功, 0: 数据无效
 */
static uint8_t flow_calc_load_from_eeprom(void)
{
    TotalFlowStorage_t storage;

    if (at24c02_read_buffer(TOTAL_FLOW_EEPROM_ADDR, sizeof(TotalFlowStorage_t),
                            (uint8_t*)&storage) != 1) {
        return 0;
    }

    /* 校验magic number */
    if (storage.magic != TOTAL_FLOW_MAGIC_NUMBER) {
        return 0;
    }

    /* 检查是否为有效数据 (NaN检查) */
    if (storage.total_flow < 0.0 || storage.total_flow >= 1e12) {
        return 0;
    }

    /* 检查累计时长是否有效 */
    if (storage.total_time > 0xFFFFFFFFUL / 2) {
        storage.total_time = 0;
    }

    s_total_flow_m3 = storage.total_flow;
    s_total_time_sec = storage.total_time;
    return 1;
}

/**
 * @brief  从备份寄存器/EEPROM加载累计流量和累计时长
 * @note   优先从备份寄存器读取，失败则从EEPROM读取
 */
void flow_calc_load_total(void)
{
    /* 优先从备份寄存器加载 */
    if (flow_calc_load_from_backup()) {
        return;
    }

    /* 备份寄存器无效，尝试从EEPROM加载 */
    if (flow_calc_load_from_eeprom()) {
        /* 同步到备份寄存器 */
        flow_calc_save_total();
        return;
    }

    /* 两者都无效，使用默认值0 */
    s_total_flow_m3 = 0.0;
    s_total_time_sec = 0;
}

/**
 * @brief  更新流量计算 (每秒调用)
 * @note   从传感器获取水位，计算瞬时流量并累加累计流量和累计时长
 *         - 瞬时流量: 根据配置的单位显示
 *         - 累计流量: 固定使用 m³
 *         - 累计时长: 单位 秒
 *         - 每10秒保存到备份寄存器
 *         - 每5分钟保存到EEPROM
 */
void flow_calc_update(void)
{
    SensorData_t *sensor;
    float water_level_m;

    /* 累计时长加1秒 */
    s_total_time_sec++;

    /* 获取传感器数据 */
    sensor = app_sensor_get_data();
    if (sensor == NULL || !sensor->is_online) {
        s_instant_flow = 0.0f;
        return;
    }

    water_level_m = sensor->water_level_m;

    /* 计算瞬时流量 (L/s) */
    s_instant_flow = flow_calc_instant(water_level_m);

    /* 累加累计流量: L/s * 1s = L, 除以1000转m³
     * 注意：使用double精度计算，避免float除法丢失精度 */
    s_total_flow_m3 += (double)s_instant_flow / 1000.0;

    /* 单位转换 */
    if (app_config_get_instant_unit() != FLOW_UNIT_L_S) {
        s_instant_flow = flow_convert_instant(s_instant_flow, app_config_get_instant_unit());
    }

    /* 每10秒保存累计流量到备份寄存器 */
    if (++s_bkp_save_counter >= 10) {
        s_bkp_save_counter = 0;
        flow_calc_save_total();
    }

    /* 每5分钟设置EEPROM保存标志 (5分钟 = 300秒) */
    if (++s_eeprom_save_counter >= 300) {
        s_eeprom_save_counter = 0;
        s_eeprom_save_pending = 1;
    }
}

/**
 * @brief  处理EEPROM保存请求 (在主循环中调用)
 * @note   避免在定时器中直接操作I2C，防止阻塞
 */
void flow_calc_process(void)
{
    if (s_eeprom_save_pending) {
        s_eeprom_save_pending = 0;
        flow_calc_save_to_eeprom();
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
 * @brief  清零累计流量和累计时长
 */
void flow_calc_reset_total(void)
{
    s_total_flow_m3 = 0.0;
    s_total_time_sec = 0;
    flow_calc_save_total();       /* 同步清除备份寄存器 */
    flow_calc_save_to_eeprom();   /* 同步清除EEPROM */
}

/**
 * @brief  获取累计时长
 * @retval 累计时长 (秒)
 */
uint32_t flow_calc_get_total_time(void)
{
    return s_total_time_sec;
}
