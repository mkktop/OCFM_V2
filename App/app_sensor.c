/**
 * @file app_sensor.c
 * @brief 传感器应用层实现 - 封装Modbus主机操作
 * @details 通过UART1(RS485)与水位传感器通信，读取距离数据
 */

#include "app_sensor.h"
#include "modbus_master.h"
#include "global.h"
#include "main.h"
#include <string.h>

/* 传感器数据实例 */
static SensorData_t g_sensor_data;

/* 传感器配置 */
#define SENSOR_SLAVE_ID     SENSOR_ADDR_1     /* 从机地址 */
#define SENSOR_START_ADDR   SENSOR_REG_DATA   /* 起始地址 */
#define SENSOR_QUANTITY     1                 /* 只读取1个寄存器(距离) */

/**
 * @brief 初始化传感器模块
 */
void app_sensor_init(void)
{
    /* 清零传感器数据 */
    memset(&g_sensor_data, 0, sizeof(SensorData_t));

    /* 初始化Modbus主机 (使用UART1) */
    modbus_master_init(&sensor_master, &huart1);

    /* 添加传感器设备 */
    modbus_master_add_sensor(&sensor_master, SENSOR_SLAVE_ID,
                              SENSOR_START_ADDR, SENSOR_QUANTITY);
}

/**
 * @brief 传感器轮询任务
 * @note 需要在循环中周期性调用，建议10ms
 */
void app_sensor_poll(void)
{
    /* 调用Modbus主机轮询 */
    modbus_master_poll(&sensor_master);

    /* 更新传感器数据缓存 */
    g_sensor_data.is_online = modbus_master_is_sensor_online(0);

    if (g_sensor_data.is_online) {
        g_sensor_data.distance = modbus_master_get_register_value(0, 0);
        g_sensor_data.last_update_time = HAL_GetTick();
    }
}

/**
 * @brief 获取距离值
 * @retval 距离值 (mm)，离线返回0
 */
uint16_t app_sensor_get_distance(void)
{
    if (g_sensor_data.is_online) {
        return g_sensor_data.distance;
    }
    return 0;
}

/**
 * @brief 检查传感器是否在线
 * @retval 1:在线 0:离线
 */
uint8_t app_sensor_is_online(void)
{
    return g_sensor_data.is_online;
}

/**
 * @brief 设置传感器参数
 * @param reg_addr: 寄存器地址
 * @param value: 设置值
 * @retval 0:成功 1:失败
 */
uint8_t app_sensor_set_param(uint16_t reg_addr, uint16_t value)
{
    if (modbus_master_write_single_register(&sensor_master, SENSOR_SLAVE_ID, reg_addr, value)) {
        return 0;
    }
    return 1;
}

/**
 * @brief 读取传感器参数
 * @param reg_addr: 寄存器地址
 * @param value: 返回值指针
 * @retval 0:成功 1:失败
 */
uint8_t app_sensor_get_param(uint16_t reg_addr, uint16_t *value)
{
    if (value == NULL) {
        return 1;
    }

    uint8_t data[4];
    if (modbus_master_read_holding_registers(&sensor_master, SENSOR_SLAVE_ID, reg_addr, 1, data)) {
        *value = (data[0] << 8) | data[1];
        return 0;
    }
    return 1;
}
