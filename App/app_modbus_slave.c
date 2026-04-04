/**
 * @file app_modbus_slave.c
 * @brief Modbus从机应用层实现 - 寄存器与业务数据的映射
 * @details 上行：周期将实时数据写入保持寄存器，供主站读取
 *          下行：主站写入寄存器时，通过回调更新配置/控制继电器
 */

#include "app_modbus_slave.h"
#include "modbus_slave.h"
#include "modbus.h"
#include "app_config.h"
#include "app_flow_calc.h"
#include "app_sensor.h"
#include "rtc_time.h"
#include "global.h"
#include <string.h>

/*============================================================================*/
/*                           延迟保存机制                                       */
/*============================================================================*/

static volatile uint8_t config_dirty = 0;
static uint32_t last_write_time = 0;
#define CONFIG_SAVE_DELAY_MS   3000

/*============================================================================*/
/*                           继电器GPIO (仅读取状态)                           */
/*============================================================================*/

static const struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} relay_gpios[] = {
    {RELAY1_CTRL_GPIO_Port, RELAY1_CTRL_Pin},
    {RELAY2_CTRL_GPIO_Port, RELAY2_CTRL_Pin},
    {RELAY3_CTRL_GPIO_Port, RELAY3_CTRL_Pin},
    {RELAY4_CTRL_GPIO_Port, RELAY4_CTRL_Pin},
};

/**
 * @brief 判断寄存器是否为多寄存器值(占2个寄存器, uint32)
 * @param addr 寄存器地址
 * @retval 1: 是多寄存器 0: 否
 */
static uint8_t is_uint32_register(uint16_t addr)
{
    return 0;  /* 所有 uint32 寄存器已迁移为 float */
}

/**
 * @brief 判断寄存器是否为 float 类型 (占2个寄存器, IEEE 754)
 */
static uint8_t is_float_register(uint16_t addr)
{
    switch (addr)
    {
        case REG_INSTANT_FLOW:
        case REG_AH:
        case REG_DH:
        case REG_AL:
        case REG_DL:
        case REG_AAH:
        case REG_AAL:
        case REG_RANGE_4MA:
        case REG_RANGE_20MA:
            return 1;
        default:
            return 0;
    }
}

/*============================================================================*/
/*                           下行：处理主站写入                                  */
/*============================================================================*/

/**
 * @brief 写入回调：处理主站对寄存器的写入
 * @param start_addr 起始寄存器地址
 * @param quantity 写入的寄存器数量
 * @note 每次写入操作(0x06/0x10)只触发一次
 *       从 holding_registers[] 读取已写入的值
 */
void app_modbus_slave_on_write(uint16_t start_addr, uint16_t quantity)
{
    /* 继电器状态寄存器 (0x000A-0x000D) 为只读，不允许写入 */

    /* 恢复出厂设置 (地址0x1006) */
    if (start_addr == REG_FACTORY_SETTING)
    {
        uint16_t value = modbus_slave_get_holding_register(REG_FACTORY_SETTING);
        if (value == 1)
        {
            app_config_factory_reset();
        }
        return;
    }

    /* 清除累计(流量+时间) (地址0x1007) */
    if (start_addr == REG_CLEAR_TOTAL)
    {
        uint16_t value = modbus_slave_get_holding_register(REG_CLEAR_TOTAL);
        if (value == 1)
        {
            flow_calc_reset_total();
        }
        return;
    }

    /* RTC时间设置寄存器 (0x0200-0x0206): 任意一个写入后立即更新RTC */
    if (start_addr >= REG_RTC_YEAR && start_addr <= REG_RTC_WEEKDAY)
    {
        uint16_t year    = modbus_slave_get_holding_register(REG_RTC_YEAR);
        uint16_t month   = modbus_slave_get_holding_register(REG_RTC_MONTH);
        uint16_t day     = modbus_slave_get_holding_register(REG_RTC_DAY);
        uint16_t hour    = modbus_slave_get_holding_register(REG_RTC_HOUR);
        uint16_t minute  = modbus_slave_get_holding_register(REG_RTC_MINUTE);
        uint16_t second  = modbus_slave_get_holding_register(REG_RTC_SECOND);
        uint16_t weekday = modbus_slave_get_holding_register(REG_RTC_WEEKDAY);

        /* 校验范围 */
        if (year >= 2000 && year <= 2099 &&
            month >= 1 && month <= 12 &&
            day >= 1 && day <= 31 &&
            hour <= 23 && minute <= 59 && second <= 59 &&
            weekday >= 1 && weekday <= 7)
        {
            RTC_Time_SetValues(year, (uint8_t)month, (uint8_t)day,
                               (uint8_t)hour, (uint8_t)minute, (uint8_t)second,
                               (uint8_t)weekday);
        }
        return;
    }

    /* 多寄存器值 (uint32, 占2个寄存器) */
    if (is_uint32_register(start_addr) && quantity >= 2)
    {
        uint32_t value = modbus_slave_get_uint32(start_addr);
        uint8_t result = app_config_set_by_reg(start_addr, value);
        if (result == 0)
        {
            config_dirty = 1;
            last_write_time = HAL_GetTick();
        }
        return;
    }

    /* float 寄存器值 (IEEE 754, 占2个寄存器) */
    if (is_float_register(start_addr) && quantity >= 2)
    {
        float fval = modbus_slave_get_float(start_addr);
        uint32_t value;
        memcpy(&value, &fval, sizeof(float));
        uint8_t result = app_config_set_by_reg(start_addr, value);
        if (result == 0)
        {
            config_dirty = 1;
            last_write_time = HAL_GetTick();
        }
        return;
    }

    /* 单寄存器值 (uint16) */
    uint16_t value = modbus_slave_get_holding_register(start_addr);
    uint8_t result = app_config_set_by_reg(start_addr, (uint32_t)value);
    if (result == 0)
    {
        config_dirty = 1;
        last_write_time = HAL_GetTick();
    }
}

/*============================================================================*/
/*                           延迟保存处理                                      */
/*============================================================================*/

/**
 * @brief 处理延迟保存请求
 */
void app_modbus_slave_process(void)
{
    if (!config_dirty)
    {
        return;
    }

    /* 检查是否超过延迟时间 */
    if (HAL_GetTick() - last_write_time >= CONFIG_SAVE_DELAY_MS)
    {
        config_dirty = 0;
        app_config_save();
    }
}

/*============================================================================*/
/*                           上行：数据同步到寄存器                             */
/*============================================================================*/

/**
 * @brief 更新Modbus从机寄存器数据
 * @note 周期调用，将实时数据同步到保持寄存器
 */
void app_modbus_slave_update(void)
{
    SensorData_t *sensor = app_sensor_get_data();

    /* 物位 (水位) - 0x0001 */
    if (sensor != NULL && sensor->is_online)
    {
        uint16_t water_level_mm = (uint16_t)(sensor->water_level_m * 1000);
        modbus_slave_set_holding_register(REG_WUWEI, water_level_mm);
    }

    /* 距离 - 0x0002 */
    if (sensor != NULL && sensor->is_online)
    {
        uint16_t distance_mm = (uint16_t)(sensor->distance_m * 1000);
        modbus_slave_set_holding_register(REG_DISTANCE, distance_mm);
    }

    /* 温度 - 0x0003 (暂无温度数据，保持0) */

    /* 瞬时流量 (float) - 0x0004 占2个寄存器 */
    {
        float instant_flow = flow_calc_get_instant();
        modbus_slave_set_float(REG_INSTANT_FLOW, instant_flow);
    }

    /* 累计流量 (double) - 0x0006 占4个寄存器 */
    {
        double total_flow = flow_calc_get_total();
        modbus_slave_set_double(REG_SUM_FLOW, total_flow);
    }

    /* 继电器状态 - 0x000A-0x000D (从GPIO读取) */
    for (uint8_t i = 0; i < 4; i++)
    {
        GPIO_PinState state = HAL_GPIO_ReadPin(relay_gpios[i].port, relay_gpios[i].pin);
        modbus_slave_set_holding_register(REG_RELAY1_STATUS + i,
                                         (state == GPIO_PIN_SET) ? 1 : 0);
    }

    /* 报警值寄存器 (float, IEEE 754) - 0x000E-0x0019 */
    modbus_slave_set_float(REG_AH, app_config_get_alarm_ah());
    modbus_slave_set_float(REG_DH, app_config_get_alarm_dh());
    modbus_slave_set_float(REG_AL, app_config_get_alarm_al());
    modbus_slave_set_float(REG_DL, app_config_get_alarm_dl());
    modbus_slave_set_float(REG_AAH, app_config_get_alarm_aah());
    modbus_slave_set_float(REG_AAL, app_config_get_alarm_aal());

    /* 传感器参数寄存器 (uint16) - 0x0065-0x006F */
    uint32_t val;

    /* 传感器参数寄存器 (uint16) - 0x0065-0x006F */
    if (app_config_get_by_reg(REG_RANGE_MAX, &val) == 0)
        modbus_slave_set_holding_register(REG_RANGE_MAX, (uint16_t)val);
    if (app_config_get_by_reg(REG_HEIGHT, &val) == 0)
        modbus_slave_set_holding_register(REG_HEIGHT, (uint16_t)val);
    if (app_config_get_by_reg(REG_L1, &val) == 0)
        modbus_slave_set_holding_register(REG_L1, (uint16_t)val);
    if (app_config_get_by_reg(REG_L2, &val) == 0)
        modbus_slave_set_holding_register(REG_L2, (uint16_t)val);
    if (app_config_get_by_reg(REG_L3, &val) == 0)
        modbus_slave_set_holding_register(REG_L3, (uint16_t)val);
    if (app_config_get_by_reg(REG_L4, &val) == 0)
        modbus_slave_set_holding_register(REG_L4, (uint16_t)val);
    if (app_config_get_by_reg(REG_L5, &val) == 0)
        modbus_slave_set_holding_register(REG_L5, (uint16_t)val);
    if (app_config_get_by_reg(REG_L6, &val) == 0)
        modbus_slave_set_holding_register(REG_L6, (uint16_t)val);
    if (app_config_get_by_reg(REG_ADDRESS, &val) == 0)
        modbus_slave_set_holding_register(REG_ADDRESS, (uint16_t)val);
    if (app_config_get_by_reg(REG_BAUDE_RATE, &val) == 0)
        modbus_slave_set_holding_register(REG_BAUDE_RATE, (uint16_t)val);
    if (app_config_get_by_reg(REG_STOP_BITS, &val) == 0)
        modbus_slave_set_holding_register(REG_STOP_BITS, (uint16_t)val);

    /* Modbus从机参数寄存器 - 0x0101-0x0107 */
    if (app_config_get_by_reg(REG_CANALS__TYPE, &val) == 0)
        modbus_slave_set_holding_register(REG_CANALS__TYPE, (uint16_t)val);
    if (app_config_get_by_reg(REG_CHANNEL_ID, &val) == 0)
        modbus_slave_set_holding_register(REG_CHANNEL_ID, (uint16_t)val);
    if (app_config_get_by_reg(REG_INSTANT_UNIT, &val) == 0)
        modbus_slave_set_holding_register(REG_INSTANT_UNIT, (uint16_t)val);
    if (app_config_get_by_reg(REG_SUM_POINT, &val) == 0)
        modbus_slave_set_holding_register(REG_SUM_POINT, (uint16_t)val);
    /* float 寄存器 - 0x0105-0x0108 */
    {
        float fval = app_config_get_range_4ma();
        modbus_slave_set_float(REG_RANGE_4MA, fval);
    }
    {
        float fval = app_config_get_range_20ma();
        modbus_slave_set_float(REG_RANGE_20MA, fval);
    }

    /* 出厂校准寄存器 - 0x1002-0x1006 */
    if (app_config_get_by_reg(REG_DEAD_ZONE, &val) == 0)
        modbus_slave_set_holding_register(REG_DEAD_ZONE, (uint16_t)val);
    if (app_config_get_by_reg(REG_DIS_OFFSET, &val) == 0)
        modbus_slave_set_holding_register(REG_DIS_OFFSET, (uint16_t)val);
    if (app_config_get_by_reg(REG_CALIBRATION_4MA, &val) == 0)
        modbus_slave_set_holding_register(REG_CALIBRATION_4MA, (uint16_t)val);
    if (app_config_get_by_reg(REG_CALIBRATION_20MA, &val) == 0)
        modbus_slave_set_holding_register(REG_CALIBRATION_20MA, (uint16_t)val);

    /* RTC时间寄存器 - 0x0200-0x0206 */
    {
        RTC_TimeData time;
        RTC_Time_Get(&time);
        modbus_slave_set_holding_register(REG_RTC_YEAR,   time.year);
        modbus_slave_set_holding_register(REG_RTC_MONTH,  time.month);
        modbus_slave_set_holding_register(REG_RTC_DAY,    time.date);
        modbus_slave_set_holding_register(REG_RTC_HOUR,   time.hour);
        modbus_slave_set_holding_register(REG_RTC_MINUTE, time.minute);
        modbus_slave_set_holding_register(REG_RTC_SECOND, time.second);
        modbus_slave_set_holding_register(REG_RTC_WEEKDAY,time.weekDay);
    }
}

/*============================================================================*/
/*                           初始化                                             */
/*============================================================================*/

/**
 * @brief 初始化Modbus从机应用层
 * @note 在系统启动时调用，将配置参数预填到寄存器
 */
void app_modbus_slave_init(void)
{
    /* 首次同步所有配置参数到寄存器 */
    app_modbus_slave_update();
}
