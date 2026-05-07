/**
 * @file app_config.c
 * @brief 系统配置管理源文件
 */

#include "app_config.h"
#include "app_flow_calc.h"
#include "at24c02.h"
#include <string.h>

/* 全局配置实例 */
static SystemConfig_t g_config;

/*============================================================================*/
/*                           延迟保存机制                                       */
/*============================================================================*/

static volatile uint8_t config_dirty = 0;
static volatile uint8_t s_eeprom_busy = 0;  /* EEPROM操作互锁 (config/flow_calc共用) */
static uint32_t last_write_tick = 0;
#define CONFIG_SAVE_DELAY_MS   3000

/* 参数变更回调 (支持多个监听者) */
#define CONFIG_MAX_CHANGE_CB  4
typedef void (*config_change_cb_t)(config_id_t id);
static config_change_cb_t g_config_change_cbs[CONFIG_MAX_CHANGE_CB] = {NULL, NULL, NULL, NULL};

static void notify_config_change(config_id_t id)
{
    for (uint8_t i = 0; i < CONFIG_MAX_CHANGE_CB; i++)
    {
        if (g_config_change_cbs[i])
        {
            g_config_change_cbs[i](id);
        }
    }
}

/**
 * @brief 处理延迟保存请求
 * @note 需要在主循环中周期性调用
 *       当有脏数据且超过延迟时间后，执行EEPROM写入
 */
void app_config_process(void)
{
    if (!config_dirty)
    {
        return;
    }

    if (HAL_GetTick() - last_write_tick >= CONFIG_SAVE_DELAY_MS)
    {
        if (app_config_save()) {
            config_dirty = 0;
        }
    }
}

/*============================================================================*/
/*                           EEPROM互锁 (多模块共用)                            */
/*============================================================================*/

uint8_t app_config_eeprom_lock(void)
{
    __disable_irq();
    if (s_eeprom_busy) {
        __enable_irq();
        return 0;
    }
    s_eeprom_busy = 1;
    __enable_irq();
    return 1;
}

void app_config_eeprom_unlock(void)
{
    s_eeprom_busy = 0;
}

/*============================================================================*/
/*                           系统配置管理                                       */
/*============================================================================*/

/**
 * @brief 使用默认值初始化配置
 * @retval None
 */
void app_config_set_default(void)
{
    memset(&g_config, 0, sizeof(SystemConfig_t));

    g_config.magic_number = CONFIG_MAGIC_NUMBER;

    // 基本参数
    g_config.range_max         = DEFAULT_RANGE_MAX;
    g_config.height            = DEFAULT_HEIGHT;
    g_config.calibration_4ma   = DEFAULT_CALIBRATION_4MA;
    g_config.calibration_20ma  = DEFAULT_CALIBRATION_20MA;
    g_config.range_4ma         = DEFAULT_RANGE_4MA;
    g_config.range_20ma        = DEFAULT_RANGE_20MA;
    g_config.point_num         = DEFAULT_POINT_NUM;

    // 测量参数
    g_config.window_width      = DEFAULT_WINDOW_WIDTH;
    g_config.filter_count      = DEFAULT_FILTER_COUNT;
    g_config.delay_time        = DEFAULT_DELAY_TIME;
    g_config.antenna_type      = DEFAULT_ANTENNA_TYPE;
    g_config.blind_area        = DEFAULT_BLIND_AREA;
    g_config.w_coeff           = DEFAULT_W_COEFF;
    g_config.m_coeff           = DEFAULT_M_COEFF;

    // Modbus从机参数
    g_config.modbusAddr        = DEFAULT_MODBUS_ADDR;
    g_config.modbusBaudRate    = DEFAULT_MODBUS_BAUD;
    g_config.modbusStopBits    = DEFAULT_MODBUS_STOP;

    // 报警参数
    g_config.alarm_ah          = DEFAULT_ALARM_AH;
    g_config.alarm_al          = DEFAULT_ALARM_AL;
    g_config.alarm_dh          = DEFAULT_ALARM_DH;
    g_config.alarm_dl          = DEFAULT_ALARM_DL;
    g_config.alarm_aah         = DEFAULT_ALARM_AAH;
    g_config.alarm_aal         = DEFAULT_ALARM_AAL;

    // 其他参数
    g_config.factory_settings  = DEFAULT_FACTORY_SETTINGS;
    g_config.dis_offset        = DEFAULT_DIS_OFFSET;
    g_config.canals_type       = DEFAULT_CANALS_TYPE;
    g_config.channel_id        = DEFAULT_CHANNEL_ID;
    g_config.instant_unit      = DEFAULT_INSTANT_UNIT;
    g_config.sum_point         = DEFAULT_SUM_POINT;
    g_config.language          = DEFAULT_LANGUAGE;
    g_config.show_alarm        = DEFAULT_SHOW_ALARM;
    g_config.password_enable   = DEFAULT_PASSWORD_ENABLE;
}

/**
 * @brief 保存系统配置到EEPROM
 * @retval 1: 成功 0: 失败
 */
uint8_t app_config_save(void)
{
    /* 防止并发EEPROM写入 */
    if (s_eeprom_busy)
    {
        return 0;
    }
    s_eeprom_busy = 1;

    /* 确保magic_number已设置 */
    g_config.magic_number = CONFIG_MAGIC_NUMBER;

    /* 拷贝到本地缓冲区后写入, 避免写入期间g_config被其他任务修改 */
    uint8_t buf[sizeof(SystemConfig_t)];
    memcpy(buf, &g_config, sizeof(SystemConfig_t));
    uint8_t ret = at24c02_write_buffer(CONFIG_EEPROM_ADDR, sizeof(SystemConfig_t), buf);

    s_eeprom_busy = 0;
    return ret;
}

/**
 * @brief 加载系统配置从EEPROM
 * @retval 1: 成功 0: 失败
 */
uint8_t app_config_load(void)
{
    return at24c02_read_buffer(CONFIG_EEPROM_ADDR, sizeof(SystemConfig_t),
                               (uint8_t*)&g_config);
}

/**
 * @brief 检查配置是否有效
 * @retval 1: 有效 0: 无效
 */
uint8_t app_config_is_valid(void)
{
    return (g_config.magic_number == CONFIG_MAGIC_NUMBER) ? 1 : 0;
}

/**
 * @brief 初始化系统配置
 * @retval None
 * @note 从EEPROM读取配置，若无效则使用默认值初始化
 */
void app_config_init(void)
{
#if CONFIG_FACTORY_RESET
    /* 强制恢复出厂设置 */
    app_config_set_default();
    app_config_save();
    return;
#endif

    /* 从EEPROM加载配置 */
    if (app_config_load() == 1) {
        /* 检查配置是否有效 */
        if (app_config_is_valid() == 1) {
            /* 配置有效，直接使用 */
            return;
        }
    }

    /* EEPROM无数据或配置无效，使用默认值 */
    app_config_set_default();

    /* 保存默认值到EEPROM */
    app_config_save();
}

/**
 * @brief 获取系统配置指针
 * @retval SystemConfig_t结构体指针
 */
SystemConfig_t* app_config_get(void)
{
    return &g_config;
}

/**
 * @brief 恢复出厂设置
 * @retval 1: 成功 0: 失败
 */
uint8_t app_config_factory_reset(void)
{
    /* 使用默认值初始化 */
    app_config_set_default();

    /* 清除脏标记 */
    config_dirty = 0;

    /* 保存到EEPROM */
    return app_config_save();
}

/*============================================================================*/
/*                           统一配置参数API                                    */
/*============================================================================*/
 
/**
 * @brief 参数范围表 (与 config_id_t 枚举顺序一一对应)
 * @note AC5不支持指定初始化器, 按顺序填充
 */
typedef struct {
    uint8_t  is_float;   /* 0=uint32, 1=float */
    uint8_t  has_range;  /* 0=不检查范围(特殊动作), 1=检查范围 */
    uint32_t min_u32;
    uint32_t max_u32;
    float    min_f;
    float    max_f;
} config_range_t;

static const config_range_t config_range_table[CONFIG_ID_COUNT] = {
    /* [0]  CONFIG_ID_RANGE_MAX       */ {0, 1, 0, 20000, 0, 0},
    /* [1]  CONFIG_ID_HEIGHT          */ {0, 1, 0, 20000, 0, 0},
    /* [2]  CONFIG_ID_CALIBRATION_4MA */ {0, 1, 0, 9999,  0, 0},
    /* [3]  CONFIG_ID_CALIBRATION_20MA*/ {0, 1, 0, 9999,  0, 0},
    /* [4]  CONFIG_ID_RANGE_4MA       */ {1, 1, 0, 0, 0.0f, 99999.0f},
    /* [5]  CONFIG_ID_RANGE_20MA      */ {1, 1, 0, 0, 0.0f, 99999.0f},
    /* [6]  CONFIG_ID_POINT_NUM       */ {0, 1, 0, 3,     0, 0},
    /* [7]  CONFIG_ID_WINDOW_WIDTH    */ {0, 1, 0, 1000,  0, 0},
    /* [8]  CONFIG_ID_FILTER_COUNT    */ {0, 1, 0, 50,    0, 0},
    /* [9]  CONFIG_ID_DELAY_TIME      */ {0, 1, 0, 1000,  0, 0},
    /* [10] CONFIG_ID_ANTENNA_TYPE    */ {0, 1, 0, 10,    0, 0},
    /* [11] CONFIG_ID_BLIND_AREA      */ {0, 1, 0, 1000,  0, 0},
    /* [12] CONFIG_ID_W_COEFF         */ {0, 1, 0, 10,    0, 0},
    /* [13] CONFIG_ID_M_COEFF         */ {0, 1, 0, 10,    0, 0},
    /* [14] CONFIG_ID_MODBUS_ADDR     */ {0, 1, 1, 247,   0, 0},
    /* [15] CONFIG_ID_MODBUS_BAUDRATE */ {0, 1, 1, 8,     0, 0},
    /* [16] CONFIG_ID_MODBUS_STOPBITS */ {0, 1, 1, 4,     0, 0},
    /* [17] CONFIG_ID_ALARM_AH        */ {1, 1, 0, 0, 0.0f, 99999.0f},
    /* [18] CONFIG_ID_ALARM_AL        */ {1, 1, 0, 0, 0.0f, 99999.0f},
    /* [19] CONFIG_ID_ALARM_DH        */ {1, 1, 0, 0, 0.0f, 99999.0f},
    /* [20] CONFIG_ID_ALARM_DL        */ {1, 1, 0, 0, 0.0f, 99999.0f},
    /* [21] CONFIG_ID_ALARM_AAH       */ {1, 1, 0, 0, 0.0f, 99999.0f},
    /* [22] CONFIG_ID_ALARM_AAL       */ {1, 1, 0, 0, 0.0f, 99999.0f},
    /* [23] CONFIG_ID_DIS_OFFSET      */ {0, 1, 0, 99999, 0, 0},
    /* [24] CONFIG_ID_CANALS_TYPE     */ {0, 1, 1, 3,     0, 0},
    /* [25] CONFIG_ID_CHANNEL_ID      */ {0, 1, 1, 16,    0, 0},
    /* [26] CONFIG_ID_INSTANT_UNIT    */ {0, 1, 1, 8,     0, 0},
    /* [27] CONFIG_ID_SUM_POINT       */ {0, 1, 0, 3,     0, 0},
    /* [28] CONFIG_ID_LANGUAGE        */ {0, 1, 0, 1,     0, 0},
    /* [29] CONFIG_ID_SHOW_ALARM      */ {0, 1, 0, 1,     0, 0},
    /* [30] CONFIG_ID_PASSWORD_ENABLE */ {0, 1, 0, 1,     0, 0},
    /* [31] CONFIG_ID_FACTORY_RESET   */ {0, 0, 0, 0,     0, 0},
    /* [31] CONFIG_ID_CLEAR_TOTAL     */ {0, 0, 0, 0,     0, 0},
};

uint8_t app_config_set(config_id_t id, uint32_t value)
{
    if (id >= CONFIG_ID_COUNT)
    {
        return CONFIG_ERR_ID;
    }

    const config_range_t *r = &config_range_table[id];

    /* 特殊动作 */
    if (!r->has_range)
    {
        if (id == CONFIG_ID_FACTORY_RESET)
        {
            if (value == 1)
            {
                app_config_factory_reset();
                notify_config_change(id);
            }
        }
        else if (id == CONFIG_ID_CLEAR_TOTAL)
        {
            if (value == 1)
            {
                flow_calc_reset_total();
                notify_config_change(id);
            }
        }
        return CONFIG_OK;
    }

    /* float参数不应通过此函数设置 */
    if (r->is_float)
    {
        return CONFIG_ERR_ID;
    }

    /* 范围检查 */
    if (value < r->min_u32 || value > r->max_u32)
    {
        return CONFIG_ERR_RANGE;
    }

    switch (id)
    {
        /* 基本参数 */
        case CONFIG_ID_RANGE_MAX:
            if (value < g_config.height) return CONFIG_ERR_RANGE;
            g_config.range_max = value;
            break;
        case CONFIG_ID_HEIGHT:
            if (value > g_config.range_max) return CONFIG_ERR_RANGE;
            g_config.height = value;
            break;
        case CONFIG_ID_CALIBRATION_4MA: g_config.calibration_4ma = value; break;
        case CONFIG_ID_CALIBRATION_20MA:g_config.calibration_20ma = value;break;
        case CONFIG_ID_POINT_NUM:       g_config.point_num = value;       break;
        /* 测量参数 */
        case CONFIG_ID_WINDOW_WIDTH:    g_config.window_width = value;    break;
        case CONFIG_ID_FILTER_COUNT:    g_config.filter_count = value;    break;
        case CONFIG_ID_DELAY_TIME:      g_config.delay_time = value;      break;
        case CONFIG_ID_ANTENNA_TYPE:    g_config.antenna_type = value;    break;
        case CONFIG_ID_BLIND_AREA:      g_config.blind_area = value;      break;
        case CONFIG_ID_W_COEFF:         g_config.w_coeff = value;         break;
        case CONFIG_ID_M_COEFF:         g_config.m_coeff = value;         break;
        /* Modbus参数 */
        case CONFIG_ID_MODBUS_ADDR:     g_config.modbusAddr = value;     break;
        case CONFIG_ID_MODBUS_BAUDRATE: g_config.modbusBaudRate = value; break;
        case CONFIG_ID_MODBUS_STOPBITS: g_config.modbusStopBits = value; break;
        /* 其他参数 */
        case CONFIG_ID_DIS_OFFSET:      g_config.dis_offset = value;      break;
        case CONFIG_ID_CANALS_TYPE:     g_config.canals_type = value;
                                        g_config.channel_id = 1;
                                        g_config.range_20ma = flow_calc_get_max_flow_m3h();
                                        if (g_config.range_4ma >= g_config.range_20ma)
                                            g_config.range_4ma = 0.0f;
                                        break;
        case CONFIG_ID_CHANNEL_ID:  {
                /* 通道编号上限取决于当前水渠类型 */
                uint32_t ch_max = 16;
                if (g_config.canals_type == 2) ch_max = 5;       /* 三角堰 */
                else if (g_config.canals_type == 3) ch_max = 4;   /* 矩形堰 */
                if (value > ch_max) return CONFIG_ERR_RANGE;
                g_config.channel_id = value;
                g_config.range_20ma = flow_calc_get_max_flow_m3h();
                if (g_config.range_4ma >= g_config.range_20ma)
                    g_config.range_4ma = 0.0f;
            }
            break;
        case CONFIG_ID_INSTANT_UNIT:    g_config.instant_unit = value;    break;
        case CONFIG_ID_SUM_POINT:       g_config.sum_point = value;       break;
        case CONFIG_ID_LANGUAGE:        g_config.language = value;        break;
        case CONFIG_ID_SHOW_ALARM:      g_config.show_alarm = value;      break;
        case CONFIG_ID_PASSWORD_ENABLE: g_config.password_enable = value; break;
        default: return CONFIG_ERR_ID;
    }

    config_dirty = 1;
    last_write_tick = HAL_GetTick();
    notify_config_change(id);
    return CONFIG_OK;
}

uint8_t app_config_setf(config_id_t id, float value)
{
    if (id >= CONFIG_ID_COUNT)
    {
        return CONFIG_ERR_ID;
    }

    const config_range_t *r = &config_range_table[id];

    if (!r->is_float)
    {
        return CONFIG_ERR_ID;
    }

    /* 范围检查 */
    if (value < r->min_f || value > r->max_f)
    {
        return CONFIG_ERR_RANGE;
    }

    /* 关联校验: AAH >= AH, AAL <= AL */
    if (id == CONFIG_ID_ALARM_AAH && value < g_config.alarm_ah)
    {
        return CONFIG_ERR_RANGE;
    }
    if (id == CONFIG_ID_ALARM_AAL && value > g_config.alarm_al)
    {
        return CONFIG_ERR_RANGE;
    }
    if (id == CONFIG_ID_ALARM_AH && value > g_config.alarm_aah)
    {
        return CONFIG_ERR_RANGE;
    }
    if (id == CONFIG_ID_ALARM_AL && value < g_config.alarm_aal)
    {
        return CONFIG_ERR_RANGE;
    }

    /* 4-20mA量程上限: 不超过当前槽型最大流量 + 10% 裕量 */
    if (id == CONFIG_ID_RANGE_20MA)
    {
        float max_flow = flow_calc_get_max_flow_m3h();
        if (max_flow > 0.0f && value > max_flow)
        {
            return CONFIG_ERR_RANGE;
        }
    }

    switch (id)
    {
        case CONFIG_ID_RANGE_4MA:  g_config.range_4ma  = value; break;
        case CONFIG_ID_RANGE_20MA: g_config.range_20ma = value; break;
        case CONFIG_ID_ALARM_AH:   g_config.alarm_ah   = value; break;
        case CONFIG_ID_ALARM_AL:   g_config.alarm_al   = value; break;
        case CONFIG_ID_ALARM_DH:   g_config.alarm_dh   = value; break;
        case CONFIG_ID_ALARM_DL:   g_config.alarm_dl   = value; break;
        case CONFIG_ID_ALARM_AAH:  g_config.alarm_aah  = value; break;
        case CONFIG_ID_ALARM_AAL:  g_config.alarm_aal  = value; break;
        default: return CONFIG_ERR_ID;
    }

    config_dirty = 1;
    last_write_tick = HAL_GetTick();
    notify_config_change(id);
    return CONFIG_OK;
}

uint8_t app_config_get_val(config_id_t id, uint32_t *value)
{
    if (id >= CONFIG_ID_COUNT || value == NULL)
    {
        return CONFIG_ERR_ID;
    }

    /* 特殊动作无读取值 */
    if (id == CONFIG_ID_FACTORY_RESET || id == CONFIG_ID_CLEAR_TOTAL)
    {
        return CONFIG_ERR_ID;
    }

    /* float参数不应通过此函数读取 */
    if (app_config_is_float(id))
    {
        return CONFIG_ERR_ID;
    }

    switch (id)
    {
        case CONFIG_ID_RANGE_MAX:       *value = g_config.range_max;       break;
        case CONFIG_ID_HEIGHT:          *value = g_config.height;          break;
        case CONFIG_ID_CALIBRATION_4MA: *value = g_config.calibration_4ma; break;
        case CONFIG_ID_CALIBRATION_20MA:*value = g_config.calibration_20ma;break;
        case CONFIG_ID_POINT_NUM:       *value = g_config.point_num;       break;
        case CONFIG_ID_WINDOW_WIDTH:    *value = g_config.window_width;    break;
        case CONFIG_ID_FILTER_COUNT:    *value = g_config.filter_count;    break;
        case CONFIG_ID_DELAY_TIME:      *value = g_config.delay_time;      break;
        case CONFIG_ID_ANTENNA_TYPE:    *value = g_config.antenna_type;    break;
        case CONFIG_ID_BLIND_AREA:      *value = g_config.blind_area;      break;
        case CONFIG_ID_W_COEFF:         *value = g_config.w_coeff;         break;
        case CONFIG_ID_M_COEFF:         *value = g_config.m_coeff;         break;
        case CONFIG_ID_MODBUS_ADDR:     *value = g_config.modbusAddr;     break;
        case CONFIG_ID_MODBUS_BAUDRATE: *value = g_config.modbusBaudRate; break;
        case CONFIG_ID_MODBUS_STOPBITS: *value = g_config.modbusStopBits; break;
        case CONFIG_ID_DIS_OFFSET:      *value = g_config.dis_offset;      break;
        case CONFIG_ID_CANALS_TYPE:     *value = g_config.canals_type;     break;
        case CONFIG_ID_CHANNEL_ID:      *value = g_config.channel_id;      break;
        case CONFIG_ID_INSTANT_UNIT:    *value = g_config.instant_unit;    break;
        case CONFIG_ID_SUM_POINT:       *value = g_config.sum_point;       break;
        case CONFIG_ID_LANGUAGE:        *value = g_config.language;        break;
        case CONFIG_ID_SHOW_ALARM:      *value = g_config.show_alarm;      break;
        case CONFIG_ID_PASSWORD_ENABLE: *value = g_config.password_enable; break;
        default: return CONFIG_ERR_ID;
    }

    return CONFIG_OK;
}

uint8_t app_config_getf(config_id_t id, float *value)
{
    if (id >= CONFIG_ID_COUNT || value == NULL)
    {
        return CONFIG_ERR_ID;
    }

    if (!app_config_is_float(id))
    {
        return CONFIG_ERR_ID;
    }

    switch (id)
    {
        case CONFIG_ID_RANGE_4MA:  *value = g_config.range_4ma;  break;
        case CONFIG_ID_RANGE_20MA: *value = g_config.range_20ma; break;
        case CONFIG_ID_ALARM_AH:   *value = g_config.alarm_ah;   break;
        case CONFIG_ID_ALARM_AL:   *value = g_config.alarm_al;   break;
        case CONFIG_ID_ALARM_DH:   *value = g_config.alarm_dh;   break;
        case CONFIG_ID_ALARM_DL:   *value = g_config.alarm_dl;   break;
        case CONFIG_ID_ALARM_AAH:  *value = g_config.alarm_aah;  break;
        case CONFIG_ID_ALARM_AAL:  *value = g_config.alarm_aal;  break;
        default: return CONFIG_ERR_ID;
    }

    return CONFIG_OK;
}

uint8_t app_config_is_float(config_id_t id)
{
    switch (id)
    {
        case CONFIG_ID_RANGE_4MA:
        case CONFIG_ID_RANGE_20MA:
        case CONFIG_ID_ALARM_AH:
        case CONFIG_ID_ALARM_AL:
        case CONFIG_ID_ALARM_DH:
        case CONFIG_ID_ALARM_DL:
        case CONFIG_ID_ALARM_AAH:
        case CONFIG_ID_ALARM_AAL:
            return 1;
        default:
            return 0;
    }
}

/**
 * @brief 注册参数变更回调
 * @param cb: 回调函数指针
 * @retval 0: 成功, 1: 已满
 */
uint8_t app_config_set_change_callback(void (*cb)(config_id_t id))
{
    for (uint8_t i = 0; i < CONFIG_MAX_CHANGE_CB; i++)
    {
        if (g_config_change_cbs[i] == cb)
        {
            return 0;
        }
    }

    for (uint8_t i = 0; i < CONFIG_MAX_CHANGE_CB; i++)
    {
        if (g_config_change_cbs[i] == NULL)
        {
            g_config_change_cbs[i] = cb;
            return 0;
        }
    }
    return 1;  /* 已满 */
}

/*============================================================================*/
/*                           基本参数 Getter/Setter                             */
/*============================================================================*/
uint32_t app_config_get_range_max(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_RANGE_MAX, &v); return v;
}
void app_config_set_range_max(uint32_t value)
{
    app_config_set(CONFIG_ID_RANGE_MAX, value);
}

uint32_t app_config_get_height(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_HEIGHT, &v); return v;
}
void app_config_set_height(uint32_t value)
{
    app_config_set(CONFIG_ID_HEIGHT, value);
}

uint32_t app_config_get_calibration_4ma(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_CALIBRATION_4MA, &v); return v;
}
void app_config_set_calibration_4ma(uint32_t value)
{
    app_config_set(CONFIG_ID_CALIBRATION_4MA, value);
}

uint32_t app_config_get_calibration_20ma(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_CALIBRATION_20MA, &v); return v;
}
void app_config_set_calibration_20ma(uint32_t value)
{
    app_config_set(CONFIG_ID_CALIBRATION_20MA, value);
}

float app_config_get_range_4ma(void)
{
    float v = 0; app_config_getf(CONFIG_ID_RANGE_4MA, &v); return v;
}
void app_config_set_range_4ma(float value)
{
    app_config_setf(CONFIG_ID_RANGE_4MA, value);
}

float app_config_get_range_20ma(void)
{
    float v = 0; app_config_getf(CONFIG_ID_RANGE_20MA, &v); return v;
}
void app_config_set_range_20ma(float value)
{
    app_config_setf(CONFIG_ID_RANGE_20MA, value);
}

uint32_t app_config_get_point_num(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_POINT_NUM, &v); return v;
}
void app_config_set_point_num(uint32_t value)
{
    app_config_set(CONFIG_ID_POINT_NUM, value);
}

/*============================================================================*/
/*                           测量参数 Getter/Setter                             */
/*============================================================================*/
uint32_t app_config_get_window_width(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_WINDOW_WIDTH, &v); return v;
}
void app_config_set_window_width(uint32_t value)
{
    app_config_set(CONFIG_ID_WINDOW_WIDTH, value);
}

uint32_t app_config_get_filter_count(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_FILTER_COUNT, &v); return v;
}
void app_config_set_filter_count(uint32_t value)
{
    app_config_set(CONFIG_ID_FILTER_COUNT, value);
}

uint32_t app_config_get_delay_time(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_DELAY_TIME, &v); return v;
}
void app_config_set_delay_time(uint32_t value)
{
    app_config_set(CONFIG_ID_DELAY_TIME, value);
}

uint32_t app_config_get_antenna_type(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_ANTENNA_TYPE, &v); return v;
}
void app_config_set_antenna_type(uint32_t value)
{
    app_config_set(CONFIG_ID_ANTENNA_TYPE, value);
}

uint32_t app_config_get_blind_area(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_BLIND_AREA, &v); return v;
}
void app_config_set_blind_area(uint32_t value)
{
    app_config_set(CONFIG_ID_BLIND_AREA, value);
}

uint32_t app_config_get_w_coeff(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_W_COEFF, &v); return v;
}
void app_config_set_w_coeff(uint32_t value)
{
    app_config_set(CONFIG_ID_W_COEFF, value);
}

uint32_t app_config_get_m_coeff(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_M_COEFF, &v); return v;
}
void app_config_set_m_coeff(uint32_t value)
{
    app_config_set(CONFIG_ID_M_COEFF, value);
}

/*============================================================================*/
/*                           Modbus参数 Getter/Setter                           */
/*============================================================================*/
uint32_t app_config_get_modbus_addr(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_MODBUS_ADDR, &v); return v;
}
void app_config_set_modbus_addr(uint32_t value)
{
    app_config_set(CONFIG_ID_MODBUS_ADDR, value);
}

uint32_t app_config_get_modbus_baudrate(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_MODBUS_BAUDRATE, &v); return v;
}
void app_config_set_modbus_baudrate(uint32_t value)
{
    app_config_set(CONFIG_ID_MODBUS_BAUDRATE, value);
}

uint32_t app_config_get_modbus_stopbits(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_MODBUS_STOPBITS, &v); return v;
}
void app_config_set_modbus_stopbits(uint32_t value)
{
    app_config_set(CONFIG_ID_MODBUS_STOPBITS, value);
}

/*============================================================================*/
/*                           报警参数 Getter/Setter                             */
/*============================================================================*/
float app_config_get_alarm_ah(void)
{
    float v = 0; app_config_getf(CONFIG_ID_ALARM_AH, &v); return v;
}
void app_config_set_alarm_ah(float value)
{
    app_config_setf(CONFIG_ID_ALARM_AH, value);
}

float app_config_get_alarm_al(void)
{
    float v = 0; app_config_getf(CONFIG_ID_ALARM_AL, &v); return v;
}
void app_config_set_alarm_al(float value)
{
    app_config_setf(CONFIG_ID_ALARM_AL, value);
}

float app_config_get_alarm_dh(void)
{
    float v = 0; app_config_getf(CONFIG_ID_ALARM_DH, &v); return v;
}
void app_config_set_alarm_dh(float value)
{
    app_config_setf(CONFIG_ID_ALARM_DH, value);
}

float app_config_get_alarm_dl(void)
{
    float v = 0; app_config_getf(CONFIG_ID_ALARM_DL, &v); return v;
}
void app_config_set_alarm_dl(float value)
{
    app_config_setf(CONFIG_ID_ALARM_DL, value);
}

float app_config_get_alarm_aah(void)
{
    float v = 0; app_config_getf(CONFIG_ID_ALARM_AAH, &v); return v;
}
void app_config_set_alarm_aah(float value)
{
    app_config_setf(CONFIG_ID_ALARM_AAH, value);
}

float app_config_get_alarm_aal(void)
{
    float v = 0; app_config_getf(CONFIG_ID_ALARM_AAL, &v); return v;
}
void app_config_set_alarm_aal(float value)
{
    app_config_setf(CONFIG_ID_ALARM_AAL, value);
}

/*============================================================================*/
/*                           其他参数 Getter/Setter                             */
/*============================================================================*/
uint32_t app_config_get_factory_settings(void)
{
    return g_config.factory_settings;
}
void app_config_set_factory_settings(uint32_t value)
{
    app_config_set(CONFIG_ID_FACTORY_RESET, value);
}

uint32_t app_config_get_dis_offset(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_DIS_OFFSET, &v); return v;
}
void app_config_set_dis_offset(uint32_t value)
{
    app_config_set(CONFIG_ID_DIS_OFFSET, value);
}

uint32_t app_config_get_canals_type(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_CANALS_TYPE, &v); return v;
}
void app_config_set_canals_type(uint32_t value)
{
    app_config_set(CONFIG_ID_CANALS_TYPE, value);
}

uint32_t app_config_get_channel_id(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_CHANNEL_ID, &v); return v;
}
void app_config_set_channel_id(uint32_t value)
{
    app_config_set(CONFIG_ID_CHANNEL_ID, value);
}

uint32_t app_config_get_instant_unit(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_INSTANT_UNIT, &v); return v;
}
void app_config_set_instant_unit(uint32_t value)
{
    app_config_set(CONFIG_ID_INSTANT_UNIT, value);
}

uint32_t app_config_get_sum_point(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_SUM_POINT, &v); return v;
}
void app_config_set_sum_point(uint32_t value)
{
    app_config_set(CONFIG_ID_SUM_POINT, value);
}

uint32_t app_config_get_language(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_LANGUAGE, &v); return v;
}
void app_config_set_language(uint32_t value)
{
    app_config_set(CONFIG_ID_LANGUAGE, value);
}

uint32_t app_config_get_show_alarm(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_SHOW_ALARM, &v); return v;
}
void app_config_set_show_alarm(uint32_t value)
{
    app_config_set(CONFIG_ID_SHOW_ALARM, value);
}

uint32_t app_config_get_password_enable(void)
{
    uint32_t v = 0; app_config_get_val(CONFIG_ID_PASSWORD_ENABLE, &v); return v;
}
void app_config_set_password_enable(uint32_t value)
{
    app_config_set(CONFIG_ID_PASSWORD_ENABLE, value);
}
