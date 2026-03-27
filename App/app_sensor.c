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
    /* 更新本地配置 */
    SystemConfig_t *config = app_config_get();
    config->height = height_mm;
    app_config_save();

    /* 下发到传感器 (寄存器地址 REG_HEIGHT = 0x0066) */
    return app_sensor_set_register(REG_HEIGHT, (uint16_t)height_mm, callback);
}

/**
 * @brief  设置传感器量程
 * @param  range_mm: 量程 (毫米)
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_range(uint32_t range_mm, void (*callback)(uint8_t result))
{
    /* 更新本地配置 */
    SystemConfig_t *config = app_config_get();
    config->range_max = range_mm;
    app_config_save();

    /* 下发到传感器 (寄存器地址 REG_RANGE_MAX = 0x0065) */
    return app_sensor_set_register(REG_RANGE_MAX, (uint16_t)range_mm, callback);
}

/**
 * @brief  设置传感器盲区
 * @param  blind_area_mm: 盲区 (毫米)
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_blind_area(uint32_t blind_area_mm, void (*callback)(uint8_t result))
{
    /* 更新本地配置 */
    SystemConfig_t *config = app_config_get();
    config->blind_area = blind_area_mm;
    app_config_save();

    /* 下发到传感器 (寄存器地址 REG_L4 = 0x006A) */
    return app_sensor_set_register(REG_L4, (uint16_t)blind_area_mm, callback);
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
