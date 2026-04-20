/**
 * @file app_modbus_slave.h
 * @brief Modbus从机应用层 - 寄存器与业务数据的映射
 * @details 负责将运行时数据同步到Modbus保持寄存器，并处理主站写入
 */

#ifndef __APP_MODBUS_SLAVE_H
#define __APP_MODBUS_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 初始化Modbus从机应用层
 * @note 在系统启动时调用一次，将配置参数写入寄存器
 */
void app_modbus_slave_init(void);

/**
 * @brief 写入回调：处理主站对寄存器的写入
 * @param start_addr 起始寄存器地址
 * @param quantity 写入的寄存器数量
 * @note 在freertos.c中通过modbus_slave_set_write_callback注册
 */
void app_modbus_slave_on_write(uint16_t start_addr, uint16_t quantity);

/**
 * @brief 更新Modbus从机寄存器数据
 * @note 需要周期性调用（建议1秒），将实时数据同步到保持寄存器：
 *       - 水位、距离、温度（传感器数据）
 *       - 瞬时流量、累计流量（流量计算数据）
 *       - 继电器状态（GPIO读取）
 *       - 报警值等配置参数
 */
void app_modbus_slave_update(void);

/**
 * @brief 处理挂起的通信参数更新
 * @note 在 Modbus 从机任务循环中调用，安全应用波特率/停止位/地址变更
 */
void app_modbus_slave_process_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MODBUS_SLAVE_H */
