/**
 * @file modbus_master.c
 * @brief Modbus主机实现 - 用于轮询485传感器
 * @details 通过串口1连接485传感器，主动发送Modbus请求并接收响应
 */

#include "modbus_master.h"
#include "modbus.h"
#include <string.h>

/**
 * @brief 全局Modbus主机实例
 * @note 用于管理485传感器的通信
 */
modbus_master_t sensor_master;

/**
 * @brief 接收数据缓冲区
 * @note 用于暂存串口接收到的Modbus响应数据
 */
static uint8_t modbus_master_rx_data[256];

/**
 * @brief 初始化Modbus主机
 * @param master 主机结构体指针
 * @param huart 串口句柄指针
 * @note 初始化所有成员变量，设置串口和初始状态
 */
void modbus_master_init(modbus_master_t *master, UART_HandleTypeDef *huart)
{
    memset(master, 0, sizeof(modbus_master_t));
    master->huart = huart;
    master->state = MODBUS_MASTER_STATE_IDLE;
    master->current_slave_index = 0;
    master->sensor_count = 0;
}

/**
 * @brief 添加传感器设备
 * @param master 主机结构体指针
 * @param slave_id 从机ID (1-247)
 * @param start_addr 寄存器起始地址
 * @param quantity 寄存器数量
 * @return 1:成功 0:失败(传感器数量已达上限)
 * @note 最多支持8个传感器同时连接
 */
uint8_t modbus_master_add_sensor(modbus_master_t *master, uint8_t slave_id,
                                  uint16_t start_addr, uint16_t quantity)
{
    /* 检查传感器数量是否已达上限 */
    if (master->sensor_count >= MODBUS_MAX_SLAVE_COUNT)
    {
        return 0;
    }

    /* 获取当前传感器结构体指针 */
    modbus_sensor_t *sensor = &master->sensors[master->sensor_count];

    /* 配置传感器参数 */
    sensor->slave_id = slave_id;           /* 从机ID */
    sensor->start_addr = start_addr;       /* 寄存器起始地址 */
    sensor->quantity = quantity;           /* 寄存器数量 */
    sensor->retry_count = 0;               /* 重试次数清零 */
    sensor->is_active = 1;                 /* 标记为激活状态 */
    sensor->last_poll_time = 0;            /* 上次轮询时间 */

    /* 传感器数量加1 */
    master->sensor_count++;

    return 1;
}

/**
 * @brief Modbus主机轮询任务
 * @param master 主机结构体指针
 * @note 需要在主循环中周期性调用，实现对所有传感器的轮询
 *       采用状态机方式：空闲→发送→等待响应→接收→处理
 */
void modbus_master_poll(modbus_master_t *master)
{
    /* 如果没有配置传感器，直接返回 */
    if (master->sensor_count == 0)
    {
        return;
    }

    /* 根据当前状态执行不同操作 */
    switch (master->state)
    {
        /* 状态1: 空闲 - 准备发送下一个请求 */
        case MODBUS_MASTER_STATE_IDLE:
        {
            /* 获取当前要轮询的传感器 */
            modbus_sensor_t *sensor = &master->sensors[master->current_slave_index];

            /* 如果传感器未激活，跳过并切换到下一个 */
            if (!sensor->is_active)
            {
                master->current_slave_index = (master->current_slave_index + 1) % master->sensor_count;
                break;
            }

            /* 构建Modbus读保持寄存器请求 (功能码0x03) */
            uint8_t req_buffer[8];
            modbus_build_request(req_buffer, sensor->slave_id, MODBUS_FUNC_READ_HOLDING,
                                 sensor->start_addr, sensor->quantity);

            /* 保存请求数据到发送缓冲区 */
            master->tx_length = 8;
            memcpy(master->tx_buffer, req_buffer, 8);

            /* 发送请求帧 */
            modbus_master_send_frame(master, master->tx_buffer, master->tx_length);

            /* 切换到等待响应状态 */
            master->state = MODBUS_MASTER_STATE_WAITING_RESPONSE;
            /* 记录超时起始时间 */
            master->timeout_start = HAL_GetTick();
            /* 清空接收缓冲区长度 */
            master->rx_length = 0;

            break;
        }

        /* 状态2: 等待响应 - 检测是否超时 */
        case MODBUS_MASTER_STATE_WAITING_RESPONSE:
        {
            /* 检查是否超时 (超过500ms无响应) */
            if ((HAL_GetTick() - master->timeout_start) > MODBUS_MASTER_TIMEOUT_MS)
            {
                /* 获取当前传感器 */
                modbus_sensor_t *sensor = &master->sensors[master->current_slave_index];
                /* 重试次数加1 */
                sensor->retry_count++;

                /* 如果重试次数超过上限，标记为离线 */
                if (sensor->retry_count >= MODBUS_MAX_RETRY)
                {
                    sensor->is_active = 0;
                    sensor->retry_count = 0;
                }

                /* 切换到下一个传感器 */
                master->current_slave_index = (master->current_slave_index + 1) % master->sensor_count;
                /* 回到空闲状态 */
                master->state = MODBUS_MASTER_STATE_IDLE;
            }
            else
            {
                /* 尝试接收数据 */
                if (modbus_master_receive_frame(master) > 0)
                {
                    /* 收到数据，切换到处理状态 */
                    master->state = MODBUS_MASTER_STATE_PROCESSING;
                }
            }
            break;
        }

        /* 状态3: 处理响应 - 解析数据 */
        case MODBUS_MASTER_STATE_PROCESSING:
        {
            /* 处理响应数据 */
            if (modbus_master_process_response(master, master->rx_buffer, master->rx_length) == 1)
            {
                /* 获取当前传感器 */
                modbus_sensor_t *sensor = &master->sensors[master->current_slave_index];
                
                /* 解析响应数据，提取寄存器值 */
                /* Modbus响应格式: [从机ID(1)][功能码(1)][字节数(1)][数据(n)][CRC(2)] */
                uint8_t byte_count = master->rx_buffer[2];  /* 数据字节数 */
                
                /* 将数据复制到传感器结构体中保存 */
                if (byte_count <= 64)
                {
                    memcpy(sensor->data, &master->rx_buffer[3], byte_count);
                    sensor->data_length = byte_count;
                }
                
                /* 通信成功，重试次数清零 */
                sensor->retry_count = 0;
                /* 记录成功轮询的时间 */
                sensor->last_poll_time = HAL_GetTick();
            }
            else
            {
                /* 通信失败 */
                modbus_sensor_t *sensor = &master->sensors[master->current_slave_index];
                sensor->retry_count++;

                /* 重试次数超过上限，标记为离线 */
                if (sensor->retry_count >= MODBUS_MAX_RETRY)
                {
                    sensor->is_active = 0;
                    sensor->retry_count = 0;
                }
            }

            /* 切换到下一个传感器 */
            master->current_slave_index = (master->current_slave_index + 1) % master->sensor_count;
            /* 回到空闲状态 */
            master->state = MODBUS_MASTER_STATE_IDLE;
            break;
        }

        /* 默认状态：回到空闲 */
        default:
            master->state = MODBUS_MASTER_STATE_IDLE;
            break;
    }
}

/**
 * @brief 读取保持寄存器 (功能码0x03)
 * @param master 主机结构体指针
 * @param slave_id 从机ID
 * @param start_addr 起始地址
 * @param quantity 寄存器数量
 * @param data 数据接收缓冲区
 * @return 1:成功 0:失败
 * @note 阻塞式等待响应，超时时间500ms
 */
uint8_t modbus_master_read_holding_registers(modbus_master_t *master, uint8_t slave_id,
                                               uint16_t start_addr, uint16_t quantity,
                                               uint8_t *data)
{
    /* 构建读保持寄存器请求 */
    uint8_t req_buffer[8];
    modbus_build_request(req_buffer, slave_id, MODBUS_FUNC_READ_HOLDING, start_addr, quantity);

    /* 发送请求 */
    modbus_master_send_frame(master, req_buffer, 8);

    /* 记录超时起始时间 */
    uint32_t timeout_start = HAL_GetTick();
    master->rx_length = 0;

    /* 等待响应循环 */
    while ((HAL_GetTick() - timeout_start) < MODBUS_MASTER_TIMEOUT_MS)
    {
        /* 尝试接收数据 */
        uint16_t rx_len = modbus_master_receive_frame(master);
        if (rx_len > 0)
        {
            /* 处理响应 */
            if (modbus_master_process_response(master, master->rx_buffer, rx_len) == 1)
            {
                /* 复制数据到用户缓冲区 */
                memcpy(data, &master->rx_buffer[3], quantity * 2);
                return 1;
            }
        }
    }

    /* 超时无响应 */
    return 0;
}

/**
 * @brief 读取输入寄存器 (功能码0x04)
 * @param master 主机结构体指针
 * @param slave_id 从机ID
 * @param start_addr 起始地址
 * @param quantity 寄存器数量
 * @param data 数据接收缓冲区
 * @return 1:成功 0:失败
 */
uint8_t modbus_master_read_input_registers(modbus_master_t *master, uint8_t slave_id,
                                            uint16_t start_addr, uint16_t quantity,
                                            uint8_t *data)
{
    /* 构建读输入寄存器请求 */
    uint8_t req_buffer[8];
    modbus_build_request(req_buffer, slave_id, MODBUS_FUNC_READ_INPUT, start_addr, quantity);

    /* 发送请求 */
    modbus_master_send_frame(master, req_buffer, 8);

    /* 记录超时起始时间 */
    uint32_t timeout_start = HAL_GetTick();
    master->rx_length = 0;

    /* 等待响应循环 */
    while ((HAL_GetTick() - timeout_start) < MODBUS_MASTER_TIMEOUT_MS)
    {
        uint16_t rx_len = modbus_master_receive_frame(master);
        if (rx_len > 0)
        {
            if (modbus_master_process_response(master, master->rx_buffer, rx_len) == 1)
            {
                memcpy(data, &master->rx_buffer[3], quantity * 2);
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief 写单个寄存器 (功能码0x06)
 * @param master 主机结构体指针
 * @param slave_id 从机ID
 * @param register_addr 寄存器地址
 * @param value 要写入的值
 * @return 1:成功 0:失败
 */
uint8_t modbus_master_write_single_register(modbus_master_t *master, uint8_t slave_id,
                                             uint16_t register_addr, uint16_t value)
{
    /* 构建写单个寄存器请求 */
    uint8_t req_buffer[8];
    req_buffer[0] = slave_id;
    req_buffer[1] = MODBUS_FUNC_WRITE_SINGLE_REG;
    req_buffer[2] = (uint8_t)(register_addr >> 8);      /* 高8位地址 */
    req_buffer[3] = (uint8_t)(register_addr & 0xFF);    /* 低8位地址 */
    req_buffer[4] = (uint8_t)(value >> 8);              /* 高8位数据 */
    req_buffer[5] = (uint8_t)(value & 0xFF);            /* 低8位数据 */

    /* 计算CRC16并添加到请求帧末尾 */
    uint16_t crc = modbus_crc16(req_buffer, 6);
    req_buffer[6] = (uint8_t)(crc & 0xFF);              /* CRC低字节 */
    req_buffer[7] = (uint8_t)(crc >> 8);                /* CRC高字节 */

    /* 发送请求 */
    modbus_master_send_frame(master, req_buffer, 8);

    /* 等待响应 */
    uint32_t timeout_start = HAL_GetTick();
    master->rx_length = 0;

    while ((HAL_GetTick() - timeout_start) < MODBUS_MASTER_TIMEOUT_MS)
    {
        uint16_t rx_len = modbus_master_receive_frame(master);
        if (rx_len > 0)
        {
            if (modbus_check_crc(master->rx_buffer, rx_len))
            {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief 写多个寄存器 (功能码0x10)
 * @param master 主机结构体指针
 * @param slave_id 从机ID
 * @param start_addr 起始地址
 * @param quantity 寄存器数量
 * @param data 要写入的数据
 * @return 1:成功 0:失败
 */
uint8_t modbus_master_write_multiple_registers(modbus_master_t *master, uint8_t slave_id,
                                                 uint16_t start_addr, uint16_t quantity,
                                                 uint8_t *data)
{
    /* 构建写多个寄存器请求 */
    uint8_t req_buffer[256];
    req_buffer[0] = slave_id;
    req_buffer[1] = MODBUS_FUNC_WRITE_MULTIPLE_REG;
    req_buffer[2] = (uint8_t)(start_addr >> 8);
    req_buffer[3] = (uint8_t)(start_addr & 0xFF);
    req_buffer[4] = (uint8_t)(quantity >> 8);
    req_buffer[5] = (uint8_t)(quantity & 0xFF);
    req_buffer[6] = (uint8_t)(quantity * 2);            /* 字节数 = 寄存器数 × 2 */

    /* 复制数据到请求帧 */
    memcpy(&req_buffer[7], data, quantity * 2);

    /* 计算CRC */
    uint16_t crc = modbus_crc16(req_buffer, 7 + quantity * 2);
    req_buffer[7 + quantity * 2] = (uint8_t)(crc & 0xFF);
    req_buffer[8 + quantity * 2] = (uint8_t)(crc >> 8);

    /* 发送请求 */
    modbus_master_send_frame(master, req_buffer, 9 + quantity * 2);

    /* 等待响应 */
    uint32_t timeout_start = HAL_GetTick();
    master->rx_length = 0;

    while ((HAL_GetTick() - timeout_start) < MODBUS_MASTER_TIMEOUT_MS)
    {
        uint16_t rx_len = modbus_master_receive_frame(master);
        if (rx_len > 0)
        {
            if (modbus_check_crc(master->rx_buffer, rx_len))
            {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief 发送Modbus数据帧
 * @param master 主机结构体指针
 * @param data 数据缓冲区
 * @param length 数据长度
 * @note 控制RS485芯片的DE引脚，实现收发切换
 *       发送前拉高DE，发送后延时再拉低DE
 */
void modbus_master_send_frame(modbus_master_t *master, uint8_t *data, uint16_t length)
{
    /* 拉高控制引脚，设置为发送模式 */
    HAL_GPIO_WritePin(UART1_CTRL_GPIO_Port, UART1_CTRL_Pin, GPIO_PIN_SET);
    HAL_Delay(1);

    /* 通过串口发送数据 */
    HAL_UART_Transmit(master->huart, data, length, 100);

    /* 发送完成后延时，确保数据发送完毕 */
    HAL_Delay(1);

    /* 拉低控制引脚，设置为接收模式 */
    HAL_GPIO_WritePin(UART1_CTRL_GPIO_Port, UART1_CTRL_Pin, GPIO_PIN_RESET);
}

/**
 * @brief 接收Modbus数据帧
 * @param master 主机结构体指针
 * @return 接收到的数据长度，0表示无数据
 * @note 采用超时方式接收，根据第三个字节确定完整帧长度
 */
uint8_t modbus_master_receive_frame(modbus_master_t *master)
{
    uint8_t ch;
    uint32_t timeout_start = HAL_GetTick();
    uint16_t rx_count = 0;

    /* 50ms超时循环接收 */
    while ((HAL_GetTick() - timeout_start) < 50)
    {
        /* 非阻塞方式接收单字节 */
        if (HAL_UART_Receive(master->huart, &ch, 1, 10) == HAL_OK)
        {
            /* 存入接收缓冲区 */
            modbus_master_rx_data[rx_count++] = ch;

            /* 已收到至少5字节，可以计算完整帧长度 */
            if (rx_count >= 5)
            {
                /* 完整帧长度 = 第三个字节(字节数) + 5(地址+功能码+字节数+CRC) */
                if (rx_count >= modbus_master_rx_data[2] + 5)
                {
                    memcpy(master->rx_buffer, modbus_master_rx_data, rx_count);
                    master->rx_length = rx_count;
                    return rx_count;
                }
            }
        }
    }

    /* 处理不完整帧（超时前已有数据） */
    if (rx_count > 0)
    {
        memcpy(master->rx_buffer, modbus_master_rx_data, rx_count);
        master->rx_length = rx_count;
        return rx_count;
    }

    return 0;
}

/**
 * @brief 处理Modbus响应数据
 * @param master 主机结构体指针
 * @param data 响应数据缓冲区
 * @param length 数据长度
 * @return 1:成功 0:失败
 * @note 验证CRC校验和功能码是否正常
 */
uint8_t modbus_master_process_response(modbus_master_t *master, uint8_t *data, uint16_t length)
{
    /* 帧长度检查，最小需要5字节 */
    if (length < 5)
    {
        return 0;
    }

    /* CRC校验 */
    if (!modbus_check_crc(data, length))
    {
        return 0;
    }

    /* 检查是否为异常响应 (功能码最高位为1) */
    if (data[1] & 0x80)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 获取传感器值
 * @param sensor_index 传感器索引
 * @param value 值输出指针
 * @return 1:成功 0:失败
 * @note 获取传感器的第一个寄存器值
 */
uint8_t modbus_master_get_sensor_value(uint8_t sensor_index, uint16_t *value)
{
    if (value == NULL)
    {
        return 0;
    }
    
    if (sensor_index >= sensor_master.sensor_count)
    {
        return 0;
    }
    
    modbus_sensor_t *sensor = &sensor_master.sensors[sensor_index];
    
    if (!sensor->is_active || sensor->data_length < 2)
    {
        return 0;
    }
    
    *value = modbus_master_get_register_value(sensor_index, 0);
    return 1;
}

/**
 * @brief 获取传感器数据
 * @param sensor_index 传感器索引
 * @return 传感器数据缓冲区指针
 */
uint8_t* modbus_master_get_sensor_data(uint8_t sensor_index)
{
    if (sensor_index < sensor_master.sensor_count)
    {
        return sensor_master.sensors[sensor_index].data;
    }
    return NULL;
}

/**
 * @brief 获取传感器数据长度
 * @param sensor_index 传感器索引
 * @return 数据长度
 */
uint16_t modbus_master_get_sensor_data_length(uint8_t sensor_index)
{
    if (sensor_index < sensor_master.sensor_count)
    {
        return sensor_master.sensors[sensor_index].data_length;
    }
    return 0;
}

/**
 * @brief 检查传感器是否在线
 * @param sensor_index 传感器索引
 * @return 1:在线 0:离线
 */
uint8_t modbus_master_is_sensor_online(uint8_t sensor_index)
{
    if (sensor_index < sensor_master.sensor_count)
    {
        return sensor_master.sensors[sensor_index].is_active;
    }
    return 0;
}

/**
 * @brief 获取寄存器值
 * @param sensor_index 传感器索引
 * @param register_index 寄存器索引 (从0开始)
 * @return 寄存器值 (16位)
 * @note 自动将两个字节组合成16位值 (大端格式)
 */
uint16_t modbus_master_get_register_value(uint8_t sensor_index, uint8_t register_index)
{
    if (sensor_index >= sensor_master.sensor_count)
    {
        return 0;
    }
    
    modbus_sensor_t *sensor = &sensor_master.sensors[sensor_index];
    
    /* 检查索引是否超出数据范围 */
    if ((register_index * 2 + 1) >= sensor->data_length)
    {
        return 0;
    }
    
    /* 组合高低字节 (大端格式: 高字节在前) */
    uint16_t value = (sensor->data[register_index * 2] << 8) | 
                     sensor->data[register_index * 2 + 1];
    
    return value;
}