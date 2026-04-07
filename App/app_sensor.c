/**
 * @file    app_sensor.c
 * @brief   传感器应用层实现 - 封装Modbus主机操作
 * @details 通过UART1(RS485)与水位传感器通信，读取距离数据
 *
 *          使用方法：
 *          1. 在系统初始化时调用 app_sensor_init()
 *          2. 创建FreeRTOS任务，每10ms调用一次 app_sensor_poll()
 *          3. 使用 app_sensor_get_distance() 获取距离值
 *          4. 使用 app_sensor_is_online() 检查传感器在线状态
 */

#include "app_sensor.h"
#include "app_config.h"
#include "modbus_master.h"
#include "global.h"
#include "usart.h"
#include "ct1820.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/*============================================================================*/
/*                           私有宏定义                                        */
/*============================================================================*/

/* 传感器配置 */
#define SENSOR_SLAVE_ID     SENSOR_ADDR_1     /* 从机地址 */
#define SENSOR_START_ADDR   SENSOR_REG_DISTANCE /* 距离寄存器地址 */
#define SENSOR_QUANTITY     SENSOR_REG_QUANTITY /* 轮询寄存器数量 */

/* 数据更新间隔 (ms) */
#define SENSOR_UPDATE_INTERVAL_MS    1000

/* 温度测量间隔 (ms) */
#define TEMP_UPDATE_INTERVAL_MS      5000

/* CT1820 转换等待时间 (ms) */
#define TEMP_CONVERT_WAIT_MS         800

/*============================================================================*/
/*                           私有变量                                          */
/*============================================================================*/

/**
 * @brief 传感器数据实例
 */
static SensorData_t g_sensor_data;

/**
 * @brief CT1820 温度轮询状态机
 */
static struct {
    uint8_t converting;         /**< 是否正在转换中 */
    uint32_t convert_start;     /**< 发起转换的时间戳 */
    uint32_t last_update;       /**< 上次完成测量的时间戳 */
} g_temp_state;

/*============================================================================*/
/*                           公共函数实现                                      */
/*============================================================================*/

/**
 * @brief  初始化传感器模块
 * @note   初始化Modbus主机并添加传感器设备
 */
void app_sensor_init(void)
{
    /* 清零传感器数据 */
    memset(&g_sensor_data, 0, sizeof(SensorData_t));

    /* 初始化CT1820温度传感器 */
    CT1820_Init();
    memset(&g_temp_state, 0, sizeof(g_temp_state));

    /* 初始化Modbus主机 */
    modbus_master_init(&sensor_master, &huart1);

    /* 添加传感器设备到轮询列表 */
    modbus_master_add_sensor(&sensor_master,
                              SENSOR_SLAVE_ID,
                              SENSOR_START_ADDR,
                              SENSOR_QUANTITY);

    /* 注册参数变更同步回调 */
    app_sensor_register_config_callback();
}

/**
 * @brief  传感器轮询任务
 * @note   需要在FreeRTOS任务中周期性调用，建议10ms
 *
 *         工作流程：
 *         1. 调用Modbus状态机处理通信
 *         2. 每1秒更新一次本地数据缓存
 */
void app_sensor_poll(void)
{
    static uint32_t last_update_time = 0;

    /* 调用Modbus主机轮询状态机 */
    modbus_master_poll(&sensor_master);

    /* 每1秒更新一次数据缓存 */
    if (HAL_GetTick() - last_update_time >= SENSOR_UPDATE_INTERVAL_MS)
    {
        last_update_time = HAL_GetTick();

        /* 更新传感器在线状态 */
        g_sensor_data.is_online = modbus_master_is_sensor_online(0);

        if (g_sensor_data.is_online)
        {
            /* 读取距离值（第一个寄存器），直接转换为米 */
            uint16_t distance_mm = modbus_master_get_register_value(0, 0);
            g_sensor_data.distance_m = distance_mm / 1000.0f;

            /* 计算水位 = 安装高度 - 距离（只有距离>0时才计算） */
            if (distance_mm > 0)
            {
                float install_height_m = app_config_get_height() / 1000.0f;
                g_sensor_data.water_level_m = install_height_m - g_sensor_data.distance_m;
            }
            else
            {
                /* 距离为0时，水位也为0（传感器未就绪） */
                g_sensor_data.water_level_m = 0.0f;
            }

            g_sensor_data.last_update_time = HAL_GetTick();
        }
    }

    /* CT1820 温度测量状态机 (每5秒一次) */
    if (g_temp_state.converting)
    {
        if (HAL_GetTick() - g_temp_state.convert_start >= TEMP_CONVERT_WAIT_MS)
        {
            int16_t temp;
            taskENTER_CRITICAL();
            temp = CT1820_GetTemp();
            taskEXIT_CRITICAL();
            if (temp != INT16_MIN)
            {
                g_sensor_data.temperature_x10 = temp;
                g_sensor_data.temp_valid = 1;
            }
            else
            {
                g_sensor_data.temp_valid = 0;
            }
            g_temp_state.converting = 0;
            g_temp_state.last_update = HAL_GetTick();
        }
    }
    else
    {
        if (HAL_GetTick() - g_temp_state.last_update >= TEMP_UPDATE_INTERVAL_MS)
        {
            taskENTER_CRITICAL();
            CT1820_StartConvert();
            taskEXIT_CRITICAL();
            g_temp_state.converting = 1;
            g_temp_state.convert_start = HAL_GetTick();
        }
    }
}

/**
 * @brief  获取距离值
 * @retval 距离值 (m)，传感器离线时返回0
 */
float app_sensor_get_distance(void)
{
    if (g_sensor_data.is_online)
    {
        return g_sensor_data.distance_m;
    }
    return 0.0f;
}

/**
 * @brief  获取温度值
 * @retval 温度值 x10 (如256=25.6°C), 0=传感器断线
 */
int16_t app_sensor_get_temperature(void)
{
    return g_sensor_data.temperature_x10;
}

/**
 * @brief  检查传感器是否在线
 * @retval 1: 在线
 * @retval 0: 离线
 */
uint8_t app_sensor_is_online(void)
{
    return g_sensor_data.is_online;
}

/**
 * @brief  获取传感器数据结构指针
 * @retval 传感器数据指针
 * @note   用于UI绑定显示
 */
SensorData_t* app_sensor_get_data(void)
{
    return &g_sensor_data;
}

/*============================================================================*/
/*                           参数设置函数实现                                    */
/*============================================================================*/

/**
 * @brief  设置传感器单个寄存器 (异步)
 * @param  reg_addr: 寄存器地址
 * @param  value: 写入值
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_register(uint16_t reg_addr, uint16_t value,
                                 void (*callback)(uint8_t result))
{
    return modbus_master_write_reg(&sensor_master, SENSOR_SLAVE_ID,
                                    reg_addr, value, callback);
}

/**
 * @brief  设置传感器安装高度 (同步到本地配置和传感器)
 * @param  height_mm: 安装高度 (毫米)
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_height(uint32_t height_mm, void (*callback)(uint8_t result))
{
    /* 更新本地配置，回调自动下发到传感器 */
    (void)callback;
    return app_config_set(CONFIG_ID_HEIGHT, height_mm) == CONFIG_OK ? 0 : 0xFF;
}

/**
 * @brief  设置传感器量程
 * @param  range_mm: 量程 (毫米)
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_range(uint32_t range_mm, void (*callback)(uint8_t result))
{
    /* 更新本地配置，回调自动下发到传感器 */
    (void)callback;
    return app_config_set(CONFIG_ID_RANGE_MAX, range_mm) == CONFIG_OK ? 0 : 0xFF;
}

/**
 * @brief  设置传感器盲区
 * @param  blind_area_mm: 盲区 (毫米)
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_blind_area(uint32_t blind_area_mm, void (*callback)(uint8_t result))
{
    /* 更新本地配置，回调自动下发到传感器 */
    (void)callback;
    return app_config_set(CONFIG_ID_BLIND_AREA, blind_area_mm) == CONFIG_OK ? 0 : 0xFF;
}

/**
 * @brief  设置传感器多个寄存器 (异步)
 * @param  start_addr: 起始寄存器地址
 * @param  quantity: 寄存器数量
 * @param  data: 写入数据数组
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_registers(uint16_t start_addr, uint16_t quantity,
                                  const uint16_t *data, void (*callback)(uint8_t result))
{
    return modbus_master_write_regs(&sensor_master, SENSOR_SLAVE_ID,
                                     start_addr, quantity, data, callback);
}

/**
 * @brief  设置传感器float参数 (异步，占2个寄存器)
 * @param  reg_addr: 起始寄存器地址
 * @param  value: float值
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 * @note   float按Modbus标准拆分为2个uint16_t (大端序)
 */
uint8_t app_sensor_set_float(uint16_t reg_addr, float value, void (*callback)(uint8_t result))
{
    /* 将float拆分为2个uint16_t (大端序) */
    uint32_t temp;
    uint16_t data[2];
    memcpy(&temp, &value, sizeof(float));
    data[0] = (uint16_t)(temp >> 16);   /* 高16位 */
    data[1] = (uint16_t)(temp & 0xFFFF); /* 低16位 */

    return app_sensor_set_registers(reg_addr, 2, data, callback);
}

/**
 * @brief  获取命令状态
 * @param  cmd_index: 命令索引 (由设置函数返回)
 * @retval 命令状态 (0=空闲/失败, 1=待处理, 2=发送中, 3=成功, 4=失败)
 */
uint8_t app_sensor_get_cmd_status(uint8_t cmd_index)
{
    return (uint8_t)modbus_master_get_cmd_status(&sensor_master, cmd_index);
}

/*============================================================================*/
/*                           参数变更同步到传感器                               */
/*============================================================================*/

/**
 * @brief 将本地配置参数下发到传感器
 * @param id: 变更的配置参数ID
 * @note 由 app_config 变更回调触发，通过UART1 Modbus写入传感器
 */
static void on_config_change_to_sensor(config_id_t id)
{
    uint16_t value;

    switch (id)
    {
        case CONFIG_ID_RANGE_MAX:
            value = (uint16_t)app_config_get_range_max();
            app_sensor_set_register(SENSOR_COM_FACTORY_LC, value, NULL);
            break;
        case CONFIG_ID_HEIGHT:
            value = (uint16_t)app_config_get_height();
            app_sensor_set_register(SENSOR_COM_GAODU, value, NULL);
            break;
        case CONFIG_ID_BLIND_AREA:
            value = (uint16_t)app_config_get_blind_area();
            app_sensor_set_register(SENSOR_COM_L4, value, NULL);
            break;
        case CONFIG_ID_WINDOW_WIDTH:
            value = (uint16_t)app_config_get_window_width();
            app_sensor_set_register(SENSOR_COM_L1, value, NULL);
            break;
        case CONFIG_ID_FILTER_COUNT:
            value = (uint16_t)app_config_get_filter_count();
            app_sensor_set_register(SENSOR_COM_L2, value, NULL);
            break;
        case CONFIG_ID_DELAY_TIME:
            value = (uint16_t)app_config_get_delay_time();
            app_sensor_set_register(SENSOR_COM_L3, value, NULL);
            break;
        case CONFIG_ID_W_COEFF:
            value = (uint16_t)app_config_get_w_coeff();
            app_sensor_set_register(SENSOR_COM_L5, value, NULL);
            break;
        case CONFIG_ID_M_COEFF:
            value = (uint16_t)app_config_get_m_coeff();
            app_sensor_set_register(SENSOR_COM_L6, value, NULL);
            break;
        case CONFIG_ID_ANTENNA_TYPE:
            value = (uint16_t)app_config_get_antenna_type();
            app_sensor_set_register(SENSOR_COM_DEAD_ZONE, value, NULL);
            break;
        case CONFIG_ID_DIS_OFFSET:
            value = (uint16_t)app_config_get_dis_offset();
            app_sensor_set_register(SENSOR_COM_DIS_OFFSET, value, NULL);
            break;
        default:
            break;
    }
}

/**
 * @brief 注册传感器参数同步回调
 * @note 在 app_sensor_init 中调用
 */
void app_sensor_register_config_callback(void)
{
    app_config_set_change_callback(on_config_change_to_sensor);
}
