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
#include "app_log.h"
#include "global.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

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

/*============================================================================*/
/*                           寄存器地址 -> config_id 映射                       */
/*============================================================================*/

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

/**
 * @brief Modbus寄存器地址 -> config_id 映射
 * @retval CONFIG_ID_COUNT 表示无对应配置
 */
static config_id_t reg_to_config_id(uint16_t reg_addr)
{
    switch (reg_addr)
    {
        case REG_RANGE_MAX:       return CONFIG_ID_RANGE_MAX;
        case REG_HEIGHT:          return CONFIG_ID_HEIGHT;
        case REG_L1:              return CONFIG_ID_WINDOW_WIDTH;
        case REG_L2:              return CONFIG_ID_FILTER_COUNT;
        case REG_L3:              return CONFIG_ID_DELAY_TIME;
        case REG_L4:              return CONFIG_ID_BLIND_AREA;
        case REG_L5:              return CONFIG_ID_W_COEFF;
        case REG_L6:              return CONFIG_ID_M_COEFF;
        case REG_ADDRESS:         return CONFIG_ID_MODBUS_ADDR;
        case REG_BAUDE_RATE:      return CONFIG_ID_MODBUS_BAUDRATE;
        case REG_STOP_BITS:       return CONFIG_ID_MODBUS_STOPBITS;
        case REG_CANALS__TYPE:    return CONFIG_ID_CANALS_TYPE;
        case REG_CHANNEL_ID:      return CONFIG_ID_CHANNEL_ID;
        case REG_INSTANT_UNIT:    return CONFIG_ID_INSTANT_UNIT;
        case REG_SUM_POINT:       return CONFIG_ID_SUM_POINT;
        case REG_RANGE_4MA:       return CONFIG_ID_RANGE_4MA;
        case REG_RANGE_20MA:      return CONFIG_ID_RANGE_20MA;
        case REG_DEAD_ZONE:       return CONFIG_ID_BLIND_AREA;
        case REG_DIS_OFFSET:      return CONFIG_ID_DIS_OFFSET;
        case REG_CALIBRATION_4MA: return CONFIG_ID_CALIBRATION_4MA;
        case REG_CALIBRATION_20MA:return CONFIG_ID_CALIBRATION_20MA;
        case REG_FACTORY_SETTING: return CONFIG_ID_FACTORY_RESET;
        case REG_CLEAR_TOTAL:     return CONFIG_ID_CLEAR_TOTAL;
        case REG_AH:              return CONFIG_ID_ALARM_AH;
        case REG_DH:              return CONFIG_ID_ALARM_DH;
        case REG_AL:              return CONFIG_ID_ALARM_AL;
        case REG_DL:              return CONFIG_ID_ALARM_DL;
        case REG_AAH:             return CONFIG_ID_ALARM_AAH;
        case REG_AAL:             return CONFIG_ID_ALARM_AAL;
        default:                  return CONFIG_ID_COUNT;
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
 */
void app_modbus_slave_on_write(uint16_t start_addr, uint16_t quantity)
{
    /* 继电器状态寄存器 (0x000A-0x000D) 为只读，不允许写入 */

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

        /* 校验范围 (含月天数合法性) */
        {
            static const uint8_t days_in_month[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
            uint8_t max_day = days_in_month[month];
            if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
                max_day = 29;
            if (!(year >= 2000 && year <= 2099 &&
                  month >= 1 && month <= 12 &&
                  day >= 1 && day <= max_day &&
                  hour <= 23 && minute <= 59 && second <= 59 &&
                  weekday >= 1 && weekday <= 7))
                return;
        }
        {
            RTC_Time_SetValues(year, (uint8_t)month, (uint8_t)day,
                               (uint8_t)hour, (uint8_t)minute, (uint8_t)second,
                               (uint8_t)weekday);
            char rtc_buf[48];
            snprintf(rtc_buf, sizeof(rtc_buf), "SET RTC %04u-%02u-%02u %02u:%02u:%02u",
                     year, month, day, hour, minute, second);
            app_log_send(LOG_TYPE_USER, rtc_buf);
        }
        return;
    }

    /* 映射到 config_id */
    config_id_t cid = reg_to_config_id(start_addr);
    if (cid >= CONFIG_ID_COUNT)
    {
        return;
    }

    /* float 寄存器 (IEEE 754, 占2个寄存器) */
    if (is_float_register(start_addr) && quantity >= 2)
    {
        float fval = modbus_slave_get_float(start_addr);
        app_config_setf(cid, fval);
        return;
    }

    /* 单寄存器值 (uint16) */
    uint16_t val16 = modbus_slave_get_holding_register(start_addr);
    app_config_set(cid, (uint32_t)val16);
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

    /* 温度 - 0x0003 */
    if (sensor != NULL)
    {
        modbus_slave_set_holding_register(REG_TEMPERATURE, (uint16_t)sensor->temperature_x10);
    }

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
    {
        float val;
        app_config_getf(CONFIG_ID_ALARM_AH, &val); modbus_slave_set_float(REG_AH, val);
        app_config_getf(CONFIG_ID_ALARM_DH, &val); modbus_slave_set_float(REG_DH, val);
        app_config_getf(CONFIG_ID_ALARM_AL, &val); modbus_slave_set_float(REG_AL, val);
        app_config_getf(CONFIG_ID_ALARM_DL, &val); modbus_slave_set_float(REG_DL, val);
        app_config_getf(CONFIG_ID_ALARM_AAH, &val); modbus_slave_set_float(REG_AAH, val);
        app_config_getf(CONFIG_ID_ALARM_AAL, &val); modbus_slave_set_float(REG_AAL, val);
    }

    /* 传感器参数寄存器 (uint16) - 0x0065-0x006F */
    {
        uint32_t val;
        app_config_get_val(CONFIG_ID_RANGE_MAX, &val);       modbus_slave_set_holding_register(REG_RANGE_MAX, (uint16_t)val);
        app_config_get_val(CONFIG_ID_HEIGHT, &val);          modbus_slave_set_holding_register(REG_HEIGHT, (uint16_t)val);
        app_config_get_val(CONFIG_ID_WINDOW_WIDTH, &val);    modbus_slave_set_holding_register(REG_L1, (uint16_t)val);
        app_config_get_val(CONFIG_ID_FILTER_COUNT, &val);    modbus_slave_set_holding_register(REG_L2, (uint16_t)val);
        app_config_get_val(CONFIG_ID_DELAY_TIME, &val);      modbus_slave_set_holding_register(REG_L3, (uint16_t)val);
        app_config_get_val(CONFIG_ID_BLIND_AREA, &val);      modbus_slave_set_holding_register(REG_L4, (uint16_t)val);
        app_config_get_val(CONFIG_ID_W_COEFF, &val);         modbus_slave_set_holding_register(REG_L5, (uint16_t)val);
        app_config_get_val(CONFIG_ID_M_COEFF, &val);         modbus_slave_set_holding_register(REG_L6, (uint16_t)val);
        app_config_get_val(CONFIG_ID_MODBUS_ADDR, &val);     modbus_slave_set_holding_register(REG_ADDRESS, (uint16_t)val);
        app_config_get_val(CONFIG_ID_MODBUS_BAUDRATE, &val); modbus_slave_set_holding_register(REG_BAUDE_RATE, (uint16_t)val);
        app_config_get_val(CONFIG_ID_MODBUS_STOPBITS, &val); modbus_slave_set_holding_register(REG_STOP_BITS, (uint16_t)val);
    }

    /* Modbus从机参数寄存器 - 0x0101-0x0107 */
    {
        uint32_t val;
        float fval;
        app_config_get_val(CONFIG_ID_CANALS_TYPE, &val);  modbus_slave_set_holding_register(REG_CANALS__TYPE, (uint16_t)val);
        app_config_get_val(CONFIG_ID_CHANNEL_ID, &val);   modbus_slave_set_holding_register(REG_CHANNEL_ID, (uint16_t)val);
        app_config_get_val(CONFIG_ID_INSTANT_UNIT, &val); modbus_slave_set_holding_register(REG_INSTANT_UNIT, (uint16_t)val);
        app_config_get_val(CONFIG_ID_SUM_POINT, &val);    modbus_slave_set_holding_register(REG_SUM_POINT, (uint16_t)val);
        app_config_getf(CONFIG_ID_RANGE_4MA, &fval);  modbus_slave_set_float(REG_RANGE_4MA, fval);
        app_config_getf(CONFIG_ID_RANGE_20MA, &fval); modbus_slave_set_float(REG_RANGE_20MA, fval);
    }

    /* 出厂校准寄存器 - 0x1001-0x1005 */
    {
        uint32_t val;
        app_config_get_val(CONFIG_ID_BLIND_AREA, &val);       modbus_slave_set_holding_register(REG_DEAD_ZONE, (uint16_t)val);
        app_config_get_val(CONFIG_ID_DIS_OFFSET, &val);      modbus_slave_set_holding_register(REG_DIS_OFFSET, (uint16_t)val);
        app_config_get_val(CONFIG_ID_CALIBRATION_4MA, &val); modbus_slave_set_holding_register(REG_CALIBRATION_4MA, (uint16_t)val);
        app_config_get_val(CONFIG_ID_CALIBRATION_20MA, &val);modbus_slave_set_holding_register(REG_CALIBRATION_20MA, (uint16_t)val);
    }

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
/*                           UART2 动态重配置                                   */
/*============================================================================*/

/**
 * @brief 波特率索引转换为实际值
 */
static uint32_t baudrate_index_to_value(uint32_t index)
{
    switch (index) {
        case 1:  return 4800;
        case 2:  return 9600;
        case 3:  return 14400;
        case 4:  return 19200;
        case 5:  return 38400;
        case 6:  return 56000;
        case 7:  return 57600;
        case 8:  return 115200;
        default: return 9600;
    }
}

/**
 * @brief 停止位索引转换为HAL常量
 * @note 1=None1StopBits, 2=Odd1StopBits, 3=None2StopBits, 4=Even1StopBits
 */
static void stopbits_index_to_hal(uint32_t index, uint32_t *stopbits, uint32_t *parity)
{
    switch (index) {
        case 1:  /* None1StopBits */
            *stopbits = UART_STOPBITS_1;
            *parity = UART_PARITY_NONE;
            break;
        case 2:  /* Odd1StopBits */
            *stopbits = UART_STOPBITS_1;
            *parity = UART_PARITY_ODD;
            break;
        case 3:  /* None2StopBits */
            *stopbits = UART_STOPBITS_2;
            *parity = UART_PARITY_NONE;
            break;
        case 4:  /* Even1StopBits */
            *stopbits = UART_STOPBITS_1;
            *parity = UART_PARITY_EVEN;
            break;
        default:
            *stopbits = UART_STOPBITS_1;
            *parity = UART_PARITY_NONE;
            break;
    }
}

/**
 * @brief 重配置UART2波特率和停止位
 */
static void reconfigure_uart2(void)
{
    uint32_t baudrate = baudrate_index_to_value(app_config_get_modbus_baudrate());
    uint32_t stopbits, parity;
    stopbits_index_to_hal(app_config_get_modbus_stopbits(), &stopbits, &parity);

    /* 停止DMA接收 */
    HAL_UART_AbortReceive(&huart2);

    /* 更新UART参数 */
    huart2.Init.BaudRate = baudrate;
    huart2.Init.StopBits = stopbits;
    huart2.Init.Parity = parity;

    /* 重新初始化UART */
    HAL_UART_Init(&huart2);

    /* 重启DMA接收 (通过重新初始化从机) */
    extern modbus_slave_t sensor_slave;
    modbus_slave_init(&sensor_slave, &huart2);
}

/**
 * @brief 参数变更回调处理
 */
static void on_config_change(config_id_t id)
{
    extern modbus_slave_t sensor_slave;

    if (id == CONFIG_ID_MODBUS_BAUDRATE || id == CONFIG_ID_MODBUS_STOPBITS)
    {
        reconfigure_uart2();
    }
    else if (id == CONFIG_ID_MODBUS_ADDR)
    {
        modbus_slave_set_id(&sensor_slave, (uint8_t)app_config_get_modbus_addr());
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
    /* 注册参数变更回调 */
    app_config_set_change_callback(on_config_change);

    /* 首次同步所有配置参数到寄存器 */
    app_modbus_slave_update();
}
