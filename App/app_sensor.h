/**
 * @file app_sensor.h
 * @brief 传感器应用层接口 - 封装Modbus主机操作
 * @details 通过UART1(RS485)与水位传感器通信，读取距离数据
 */

#ifndef __APP_SENSOR_H
#define __APP_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 传感器数据结构
 */
typedef struct
{
    uint16_t distance;          /* 距离值 (mm) */
    uint8_t is_online;          /* 传感器在线状态 */
    uint32_t last_update_time;  /* 最后更新时间戳 */
} SensorData_t;

/**
 * @brief 初始化传感器模块
 */
void app_sensor_init(void);

/**
 * @brief 传感器轮询任务
 * @note 需要在循环中周期性调用，建议10ms
 */
void app_sensor_poll(void);

/**
 * @brief 获取距离值
 * @retval 距离值 (mm)，离线返回0
 */
uint16_t app_sensor_get_distance(void);

/**
 * @brief 检查传感器是否在线
 * @retval 1:在线 0:离线
 */
uint8_t app_sensor_is_online(void);

/**
 * @brief 设置传感器参数
 * @param reg_addr: 寄存器地址
 * @param value: 设置值
 * @retval 0:成功 1:失败
 */
uint8_t app_sensor_set_param(uint16_t reg_addr, uint16_t value);

/**
 * @brief 读取传感器参数
 * @param reg_addr: 寄存器地址
 * @param value: 返回值指针
 * @retval 0:成功 1:失败
 */
uint8_t app_sensor_get_param(uint16_t reg_addr, uint16_t *value);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SENSOR_H */
