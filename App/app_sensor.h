/**
 * @file    app_sensor.h
 * @brief   传感器应用层接口 - 封装Modbus主机操作
 * @details 通过UART1(RS485)与水位传感器通信，读取距离数据
 *
 *          使用流程：
 *          @code
 *          // 1. 初始化
 *          app_sensor_init();
 *
 *          // 2. 创建任务（10ms周期）
 *          void sensor_task(void *arg) {
 *              for (;;) {
 *                  app_sensor_poll();
 *                  osDelay(10);
 *              }
 *          }
 *
 *          // 3. 获取数据
 *          uint16_t distance = app_sensor_get_distance();
 *          uint8_t online = app_sensor_is_online();
 *          @endcode
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
    uint16_t distance;          /**< 距离值 (mm) */
    uint8_t is_online;          /**< 传感器在线状态 (1:在线 0:离线) */
    uint32_t last_update_time;  /**< 最后更新时间戳 (ms) */
} SensorData_t;

/*============================================================================*/
/*                           初始化与轮询                                       */
/*============================================================================*/

/**
 * @brief  初始化传感器模块
 * @note   在系统启动时调用一次
 */
void app_sensor_init(void);

/**
 * @brief  传感器轮询任务
 * @note   需要在FreeRTOS任务中周期性调用，建议10ms
 *         数据每1秒自动更新一次
 */
void app_sensor_poll(void);

/*============================================================================*/
/*                           数据获取                                          */
/*============================================================================*/

/**
 * @brief  获取距离值
 * @retval 距离值 (mm)，传感器离线时返回0
 */
uint16_t app_sensor_get_distance(void);

/**
 * @brief  检查传感器是否在线
 * @retval 1: 在线
 * @retval 0: 离线
 */
uint8_t app_sensor_is_online(void);

/**
 * @brief  获取传感器数据结构指针
 * @retval 传感器数据指针
 * @note   用于UI绑定或批量读取数据
 */
SensorData_t* app_sensor_get_data(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SENSOR_H */
