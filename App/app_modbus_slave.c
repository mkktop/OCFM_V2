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
#include "app_current.h"
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

static volatile uint8_t s_pending_uart_reconfigure = 0;
static volatile uint8_t s_pending_slave_addr_update = 0;

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
        case REG_AH:
        case REG_DH:
        case REG_AL:
        case REG_DL:
        case REG_AAH:
        case REG_AAL:
        case REG_RANGE_4MA:
        case REG_RANGE_20MA:
        case REG_WATER_LEVEL_UP:
        case REG_WATER_LEVEL_DOWN:
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
        case REG_CHANNEL_WIDTH:   return CONFIG_ID_CHANNEL_WIDTH;
        case REG_WEIR_HEIGHT:     return CONFIG_ID_WEIR_HEIGHT;
        case REG_WATER_LEVEL_UP:  return CONFIG_ID_WATER_LEVEL_UP;
        case REG_WATER_LEVEL_DOWN: return CONFIG_ID_WATER_LEVEL_DOWN;
        case REG_INSTANT_UNIT:    return CONFIG_ID_INSTANT_UNIT;
        case REG_SUM_POINT:       return CONFIG_ID_SUM_POINT;
        case REG_RANGE_4MA:       return CONFIG_ID_RANGE_4MA;
        case REG_RANGE_20MA:      return CONFIG_ID_RANGE_20MA;
        case REG_ANTENNA_TYPE:    return CONFIG_ID_ANTENNA_TYPE;
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

static void apply_uint16_config_register(uint16_t reg_addr)
{
    config_id_t cid = reg_to_config_id(reg_addr);
    uint16_t val16;

    if (cid >= CONFIG_ID_COUNT) {
        return;
    }

    val16 = modbus_slave_get_holding_register(reg_addr);

    if (cid == CONFIG_ID_CALIBRATION_4MA || cid == CONFIG_ID_CALIBRATION_20MA) {
        app_config_set(cid, (uint32_t)val16);
        app_current_set_calibration_modbus(val16);
        return;
    }

    app_config_set(cid, (uint32_t)val16);

    /* 一次性动作寄存器，执行后立即清零 */
    if (cid == CONFIG_ID_FACTORY_RESET || cid == CONFIG_ID_CLEAR_TOTAL) {
        modbus_slave_set_holding_register(reg_addr, 0);
    }
}

/**
 * @brief 应用RTC时间设置 (整组校验后写入RTC)
 * @note 读取 0x0200-0x0206 全部7个寄存器, 校验合法后更新RTC, 失败则丢弃
 */
static void apply_rtc_group(void)
{
    uint16_t year    = modbus_slave_get_holding_register(REG_RTC_YEAR);
    uint16_t month   = modbus_slave_get_holding_register(REG_RTC_MONTH);
    uint16_t day     = modbus_slave_get_holding_register(REG_RTC_DAY);
    uint16_t hour    = modbus_slave_get_holding_register(REG_RTC_HOUR);
    uint16_t minute  = modbus_slave_get_holding_register(REG_RTC_MINUTE);
    uint16_t second  = modbus_slave_get_holding_register(REG_RTC_SECOND);
    uint16_t weekday = modbus_slave_get_holding_register(REG_RTC_WEEKDAY);

    /* 校验范围 (含月天数合法性; month>12 时防 days_in_month[] 越界读) */
    static const uint8_t days_in_month[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t max_day = (month < 13u) ? days_in_month[month] : 0;
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
        max_day = 29;
    if (!(year >= 2000 && year <= 2099 &&
          month >= 1 && month <= 12 &&
          day >= 1 && day <= max_day &&
          hour <= 23 && minute <= 59 && second <= 59 &&
          weekday >= 1 && weekday <= 7))
        return;

    RTC_Time_SetValues(year, (uint8_t)month, (uint8_t)day,
                       (uint8_t)hour, (uint8_t)minute, (uint8_t)second,
                       (uint8_t)(weekday % 7 + 1));
    char rtc_buf[48];
    snprintf(rtc_buf, sizeof(rtc_buf), "SET RTC %04u-%02u-%02u %02u:%02u:%02u",
             year, month, day, hour, minute, second);
    app_log_send(LOG_TYPE_USER, rtc_buf);
}

/**
 * @brief 写入回调：处理主站对寄存器的写入 (支持 0x06 单写 / 0x10 连写)
 * @param start_addr 起始寄存器地址
 * @param quantity 写入的寄存器数量
 * @note 每次写入操作(0x06/0x10)只触发一次回调。遍历 [start_addr, end_addr]
 *       逐个应用, 连写多个参数都会生效。
 *       只读寄存器已由协议层 (modbus_slave_addr_writable) 拦截, 不会进入此处。
 *       持久化为3秒去抖, 遍历内多次 set/setf 合并为1次EEPROM写。
 */
void app_modbus_slave_on_write(uint16_t start_addr, uint16_t quantity)
{
    uint16_t addr = start_addr;
    uint16_t end_addr;

    if (quantity == 0 || quantity > 123) {
        return;
    }
    end_addr = (uint16_t)(start_addr + quantity - 1);

    while (addr <= end_addr)
    {
        /* RTC时间设置区 (0x0200-0x0206): 整组读取校验后更新RTC, 跳出该区 */
        if (addr >= REG_RTC_YEAR && addr <= REG_RTC_WEEKDAY)
        {
            apply_rtc_group();
            addr = REG_RTC_WEEKDAY + 1;
            continue;
        }

        /* 累计流量 (double, 占4个寄存器): 需完整4字才更新, 否则跳过整个槽位 */
        if (addr == REG_SUM_FLOW)
        {
            if (end_addr >= REG_SUM_FLOW + 3)
            {
                flow_calc_set_total(modbus_slave_get_double(addr));
            }
            addr = REG_SUM_FLOW + 4;
            continue;
        }

        /* float 寄存器 (IEEE754, 占2个寄存器): 需完整2字才更新 */
        if (is_float_register(addr))
        {
            if (end_addr >= (uint16_t)(addr + 1))
            {
                config_id_t fcid = reg_to_config_id(addr);
                if (fcid < CONFIG_ID_COUNT)
                {
                    app_config_setf(fcid, modbus_slave_get_float(addr));
                }
            }
            addr = (uint16_t)(addr + 2);
            continue;
        }

        /* uint16 配置寄存器 (含通信参数/校准/一次性动作, 统一走 apply) */
        if (reg_to_config_id(addr) < CONFIG_ID_COUNT)
        {
            apply_uint16_config_register(addr);
        }
        addr = (uint16_t)(addr + 1);
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
    SensorData_t sensor_snap;
    app_sensor_get_snapshot(&sensor_snap);

    /* 校准模式超时检测 */
    app_current_calibration_tick();

    /* 物位 (水位) - 0x0001
     * 使用与流量计算相同的水位快照，确保水位和流量寄存器一致 */
    {
        float water_level = flow_calc_get_last_water_level();
        if (water_level > 0.0f)
        {
            uint16_t water_level_mm = (uint16_t)(water_level * 1000.0f + 0.5f);
            modbus_slave_set_holding_register(REG_WUWEI, water_level_mm);
        }
        else
        {
            modbus_slave_set_holding_register(REG_WUWEI, 0);
        }
    }

    /* 距离 - 0x0002 */
    if (sensor_snap.is_online)
    {
        uint16_t distance_mm = (uint16_t)(sensor_snap.distance_m * 1000);
        modbus_slave_set_holding_register(REG_DISTANCE, distance_mm);
    }

    /* 温度 - 0x0003 */
    {
        modbus_slave_set_holding_register(REG_TEMPERATURE, (uint16_t)sensor_snap.temperature_x10);
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

    /* 累计计量时间 (uint32, 秒) - 0x001A 占2个寄存器 (只读输出, 对应 REG_TOTAL_TIME) */
    modbus_slave_set_uint32(REG_TOTAL_TIME, flow_calc_get_total_time());

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
        app_config_get_val(CONFIG_ID_CHANNEL_WIDTH, &val); modbus_slave_set_holding_register(REG_CHANNEL_WIDTH, (uint16_t)val);
        app_config_get_val(CONFIG_ID_WEIR_HEIGHT, &val);   modbus_slave_set_holding_register(REG_WEIR_HEIGHT, (uint16_t)val);
        app_config_getf(CONFIG_ID_WATER_LEVEL_UP, &fval);   modbus_slave_set_float(REG_WATER_LEVEL_UP, fval);
        app_config_getf(CONFIG_ID_WATER_LEVEL_DOWN, &fval); modbus_slave_set_float(REG_WATER_LEVEL_DOWN, fval);
    }

    /* 出厂校准寄存器 - 0x1001-0x1005 */
    {
        uint32_t val;
        app_config_get_val(CONFIG_ID_ANTENNA_TYPE, &val);     modbus_slave_set_holding_register(REG_ANTENNA_TYPE, (uint16_t)val);
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
        modbus_slave_set_holding_register(REG_RTC_WEEKDAY, (time.weekDay + 5) % 7 + 1);
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
    huart2.Init.WordLength = (parity == UART_PARITY_NONE) ? UART_WORDLENGTH_8B : UART_WORDLENGTH_9B;

    /* 重新初始化UART */
    HAL_UART_Init(&huart2);

    /* 重启DMA接收，不清空寄存器和从机地址 */
    modbus_slave_restart_rx(&sensor_slave, &huart2);
}

/**
 * @brief 参数变更回调处理
 */
static void on_config_change(config_id_t id)
{
    if (id == CONFIG_ID_MODBUS_BAUDRATE ||
        id == CONFIG_ID_MODBUS_STOPBITS ||
        id == CONFIG_ID_FACTORY_RESET)
    {
        s_pending_uart_reconfigure = 1;
    }

    if (id == CONFIG_ID_MODBUS_ADDR || id == CONFIG_ID_FACTORY_RESET)
    {
        s_pending_slave_addr_update = 1;
    }
}

void app_modbus_slave_process_pending(void)
{
    if (s_pending_uart_reconfigure) {
        s_pending_uart_reconfigure = 0;
        reconfigure_uart2();
    }

    if (s_pending_slave_addr_update) {
        s_pending_slave_addr_update = 0;
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

    /* 应用EEPROM中的波特率和停止位配置到UART2 */
    reconfigure_uart2();

    /* 应用EEPROM中的从机地址 */
    modbus_slave_set_id(&sensor_slave, (uint8_t)app_config_get_modbus_addr());

 
    /* 首次同步所有配置参数到寄存器 */
    app_modbus_slave_update();
}
