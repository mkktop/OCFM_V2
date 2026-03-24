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
#include <string.h>

/*============================================================================*/
/*                           私有宏定义                                        */
/*============================================================================*/

/* 传感器配置 */
#define SENSOR_SLAVE_ID     SENSOR_ADDR_1     /* 从机地址 */
#define SENSOR_START_ADDR   0x0005            /* 距离寄存器地址 */
#define SENSOR_QUANTITY     1                 /* 只读取1个寄存器(距离) */

/* 数据更新间隔 (ms) */
#define SENSOR_UPDATE_INTERVAL_MS    1000

/*============================================================================*/
/*                           私有变量                                          */
/*============================================================================*/

/**
 * @brief 传感器数据实例
 */
static SensorData_t g_sensor_data;

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

    /* 初始化Modbus主机 */
    modbus_master_init(&sensor_master, &huart1);

    /* 添加传感器设备到轮询列表 */
    modbus_master_add_sensor(&sensor_master,
                              SENSOR_SLAVE_ID,
                              SENSOR_START_ADDR,
                              SENSOR_QUANTITY);
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
