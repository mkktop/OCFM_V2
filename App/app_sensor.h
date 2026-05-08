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
    float distance_m;           /**< 距离值 - 传感器到水面 */
    float water_level_m;        /**< 水位值 - 安装高度 - 距离 */
    uint8_t is_online;          /**< 传感器在线状态 (1:在线 0:离线) */
    uint32_t last_update_time;  /**< 最后更新时间戳 (ms) */
    int16_t temperature_x10;    /**< 温度值 x10 (如256=25.6°C) */
    uint8_t temp_valid;         /**< 温度数据有效 (1:有效 0:断线) */
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
 * @retval 距离值 (m)，传感器离线时返回0
 */
float app_sensor_get_distance(void);

/**
 * @brief  获取温度值
 * @retval 温度值 x10 (如256=25.6°C), 0=传感器断线
 */
int16_t app_sensor_get_temperature(void);

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

/**
 * @brief  获取传感器数据快照
 * @param  out: 输出缓冲区指针
 * @note   在临界区中拷贝完整数据，保证多字段一致性
 *         适用于定时器回调等需要原子读取的场景
 */
void app_sensor_get_snapshot(SensorData_t *out);

/*============================================================================*/
/*                           参数设置函数                                       */
/*============================================================================*/

/**
 * @brief  设置传感器单个寄存器 (异步)
 * @param  reg_addr: 寄存器地址
 * @param  value: 写入值
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 * @note   非阻塞调用，结果通过回调或轮询 app_sensor_get_cmd_status() 获取
 */
uint8_t app_sensor_set_register(uint16_t reg_addr, uint16_t value,
                                 void (*callback)(uint8_t result));

/**
 * @brief  设置传感器安装高度 (同步到本地配置和传感器)
 * @param  height_mm: 安装高度 (毫米)
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_height(uint32_t height_mm, void (*callback)(uint8_t result));

/**
 * @brief  设置传感器量程
 * @param  range_mm: 量程 (毫米)
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_range(uint32_t range_mm, void (*callback)(uint8_t result));

/**
 * @brief  设置传感器盲区
 * @param  blind_area_mm: 盲区 (毫米)
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_blind_area(uint32_t blind_area_mm, void (*callback)(uint8_t result));

/**
 * @brief  设置传感器多个寄存器 (异步，用于float等32位数据)
 * @param  start_addr: 起始寄存器地址
 * @param  quantity: 寄存器数量 (2个=float, 4个=double)
 * @param  data: 写入数据数组
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_registers(uint16_t start_addr, uint16_t quantity,
                                  const uint16_t *data, void (*callback)(uint8_t result));

/**
 * @brief  设置传感器float参数 (异步，占2个寄存器)
 * @param  reg_addr: 起始寄存器地址
 * @param  value: float值
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败)
 */
uint8_t app_sensor_set_float(uint16_t reg_addr, float value, void (*callback)(uint8_t result));

/**
 * @brief  获取命令状态
 * @param  cmd_index: 命令索引 (由设置函数返回)
 * @retval 命令状态
 */
uint8_t app_sensor_get_cmd_status(uint8_t cmd_index);

/**
 * @brief 注册参数变更同步回调
 * @note 在 app_sensor_init() 中内部调用
 */
void app_sensor_register_config_callback(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SENSOR_H */
