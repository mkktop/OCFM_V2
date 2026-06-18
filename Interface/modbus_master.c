/**
 * @file    modbus_master.c
 * @brief   Modbus主机通信模块实现
 * @details 本文件实现了Modbus RTU主机功能，用于通过RS485总线轮询外部传感器设备。
 *
 *          主要特性：
 *          - 支持多传感器轮询（最多MODBUS_MAX_SLAVE_COUNT个）
 *          - 非阻塞状态机架构，适合在FreeRTOS任务中调用
 *          - 使用DMA+空闲中断实现高效数据接收
 *          - 支持自动重试和离线检测
 *          - 提供阻塞式和非阻塞式两种读写接口
 *
 *          硬件连接：
 *          - UART1: RS485通信接口
 *          - UART1_CTRL_Pin: RS485收发方向控制引脚（高电平发送，低电平接收）
 *
 *          使用方法：
 *          1. 调用 modbus_master_init() 初始化主机
 *          2. 调用 modbus_master_add_sensor() 添加传感器设备
 *          3. 在主循环或定时任务中调用 modbus_master_poll() 进行轮询
 *          4. 使用 modbus_master_get_sensor_value() 等函数获取数据
 *
 * @author  OCFM_V2 Team
 * @date    2025
 * @version 1.0
 */

#include "modbus_master.h"
#include "modbus_slave.h"
#include "modbus.h"
#include <string.h>

/*============================================================================*/
/*                           全局变量定义                                       */
/*============================================================================*/

/**
 * @brief 全局Modbus主机实例
 * @note  用于管理水位传感器等外部Modbus设备的通信
 */
modbus_master_t sensor_master;

/*============================================================================*/
/*                           私有变量定义                                       */
/*============================================================================*/

/**
 * @brief DMA接收缓冲区
 * @note  用于HAL_UARTEx_ReceiveToIdle_DMA函数的接收缓冲区
 *        大小设置为128字节，足够容纳Modbus RTU响应帧
 */
static uint8_t dma_rx_buffer[128];

/**
 * @brief 接收完成标志
 * @note  当空闲中断触发时置1，表示一帧数据接收完成
 *        在 modbus_master_check_rx_complete() 中被清除
 */
static volatile uint8_t rx_complete_flag = 0;

/**
 * @brief 接收数据长度
 * @note  记录最近一次接收到的数据字节数
 */
static volatile uint16_t rx_complete_length = 0;

/*============================================================================*/
/*                           初始化与配置函数                                   */
/*============================================================================*/

/**
 * @brief  初始化Modbus主机
 * @param  master: 主机结构体指针，指向要初始化的modbus_master_t实例
 * @param  huart:  串口句柄指针，指定用于Modbus通信的UART外设
 * @note   初始化内容包括：
 *         - 清零主机结构体
 *         - 绑定串口句柄
 *         - 设置初始状态为IDLE
 *         - 启动DMA+空闲中断接收模式
 *         - 初始化命令队列
 * @retval 无
 */
void modbus_master_init(modbus_master_t *master, UART_HandleTypeDef *huart)
{
    /* 清零整个结构体，确保所有字段初始值为0 */
    memset(master, 0, sizeof(modbus_master_t));

    /* 绑定串口句柄 */
    master->huart = huart;

    /* 设置初始状态为空闲 */
    master->state = MODBUS_MASTER_STATE_IDLE;

    /* 初始化轮询索引和传感器计数 */
    master->current_slave_index = 0;
    master->sensor_count = 0;

    /* 初始化命令队列 */
    master->cmd_head = 0;
    master->cmd_tail = 0;
    master->cmd_count = 0;
    master->current_cmd = NULL;

    /*
     * 启动DMA+空闲中断接收模式
     * 工作原理：
     * - DMA持续将接收到的数据存入dma_rx_buffer
     * - 当检测到空闲帧（数据流停止）时，触发空闲中断
     * - 在中断回调中获取已接收的数据长度
     */
    HAL_UARTEx_ReceiveToIdle_DMA(huart, dma_rx_buffer, sizeof(dma_rx_buffer));
}

/**
 * @brief  添加传感器设备到轮询列表
 * @param  master:     主机结构体指针
 * @param  slave_id:   从机ID，范围1-247（Modbus协议规定）
 * @param  start_addr: 寄存器起始地址，指定要读取的寄存器首地址
 * @param  quantity:   寄存器数量，指定要读取的寄存器个数
 * @retval 1: 添加成功
 * @retval 0: 添加失败（传感器数量已达上限）
 * @note   每个传感器会在轮询时自动读取指定的寄存器区域
 */
uint8_t modbus_master_add_sensor(modbus_master_t *master, uint8_t slave_id,
                                  uint16_t start_addr, uint16_t quantity)
{
    /* 检查是否已达到最大传感器数量 */
    if (master->sensor_count >= MODBUS_MAX_SLAVE_COUNT)
    {
        return 0;
    }

    /* 获取当前传感器槽位的指针 */
    modbus_sensor_t *sensor = &master->sensors[master->sensor_count];

    /* 配置传感器参数 */
    sensor->slave_id = slave_id;        /* Modbus从机地址 */
    sensor->start_addr = start_addr;    /* 寄存器起始地址 */
    sensor->quantity = quantity;        /* 寄存器数量 */
    sensor->retry_count = 0;            /* 重试计数器清零 */
    sensor->is_active = 0;              /* 默认离线，首次成功通信后置在线 */
    sensor->last_poll_time = 0;         /* 上次轮询时间清零 */

    /* 传感器计数增加 */
    master->sensor_count++;

    return 1;
}

/*============================================================================*/
/*                           接收处理函数                                       */
/*============================================================================*/

/**
 * @brief  串口空闲中断回调函数
 * @param  huart: 触发中断的串口句柄
 * @param  size:  接收到的数据字节数
 * @note   本函数在以下情况被调用：
 *         1. HAL_UARTEx_RxEventCallback() 中断回调中
 *         2. USART1_IRQHandler 中直接调用（如果未使用HAL回调机制）
 *
 *         处理流程：
 *         1. 检查是否为Modbus主机使用的串口
 *         2. 将DMA缓冲区数据复制到主机接收缓冲区
 *         3. 设置接收完成标志
 *         4. 重新启动DMA接收，准备接收下一帧
 * @retval 无
 */
void modbus_master_rx_idle_callback(UART_HandleTypeDef *huart, uint16_t size)
{
    /* 检查是否为Modbus主机使用的串口 */
    if (huart == sensor_master.huart)
    {
        /* 防止溢出rx_buffer[80] */
        uint16_t copy_len = (size > sizeof(sensor_master.rx_buffer))
                            ? sizeof(sensor_master.rx_buffer) : size;
        memcpy(sensor_master.rx_buffer, dma_rx_buffer, copy_len);
        sensor_master.rx_length = copy_len;

        /* 记录接收完成信息 */
        rx_complete_length = size;
        rx_complete_flag = 1;

        /*
         * 清除DMA缓冲区并重新启动接收
         * 注意：必须在处理完当前数据后清除并重新启动，避免旧数据残留
         */
        memset(dma_rx_buffer, 0, sizeof(dma_rx_buffer));
        HAL_UARTEx_ReceiveToIdle_DMA(huart, dma_rx_buffer, sizeof(dma_rx_buffer));
    }
}

/**
 * @brief  HAL UART 事件回调函数（空闲中断）
 * @param  huart: 触发回调的串口句柄
 * @param  size:  接收到的数据字节数
 * @note   这是HAL库的标准回调函数，会在以下情况自动调用：
 *         - 使用HAL_UARTEx_ReceiveToIdle_DMA()启动接收后
 *         - 检测到空闲帧时
 *
 *         分别处理UART1（主机）和UART2（从机）的事件
 * @retval 无
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    modbus_master_rx_idle_callback(huart, size);
    modbus_slave_rx_idle_callback(huart, size);
}

/**
 * @brief  检查是否收到完整的Modbus响应帧
 * @retval 1: 已收到完整数据帧
 * @retval 0: 未收到数据或数据不完整
 * @note   此函数会自动清除接收完成标志，因此每次接收只能检测一次
 */
static uint8_t modbus_master_check_rx_complete(void)
{
    if (rx_complete_flag)
    {
        rx_complete_flag = 0;  /* 清除标志，为下次接收做准备 */
        return 1;
    }
    return 0;
}

/*============================================================================*/
/*                           私有函数声明                                       */
/*============================================================================*/

static uint8_t modbus_master_pop_cmd(modbus_master_t *master, modbus_cmd_t *cmd);
static void modbus_master_finish_cmd(modbus_master_t *master, uint8_t success);

/*============================================================================*/
/*                           轮询状态机                                         */
/*============================================================================*/

/**
 * @brief  Modbus主机轮询任务
 * @param  master: 主机结构体指针
 * @note   这是一个非阻塞的状态机，需要在主循环或定时任务中周期性调用。
 *
 *         状态机工作流程：
 *         @code
 *         IDLE -> WAITING_RESPONSE -> PROCESSING -> IDLE -> ...
 *         @endcode
 *
 *         优先级：命令队列 > 传感器轮询
 *
 *         状态说明：
 *         - IDLE: 空闲状态，优先检查命令队列，然后检查传感器轮询
 *         - WAITING_RESPONSE: 等待响应，检查超时和接收完成
 *         - PROCESSING: 处理响应数据，更新传感器状态或命令状态
 *
 * @retval 无
 */
void modbus_master_poll(modbus_master_t *master)
{
    /*
     * UART错误自动恢复
     * DMA模式下，任何UART错误(FE/ORE/NE)都会导致HAL中止DMA接收，
     * 使RxState变为READY。此处检测到异常后重新启动DMA+空闲中断接收。
     */
    if (master->huart != NULL && master->huart->RxState != HAL_UART_STATE_BUSY_RX)
    {
        __HAL_UART_CLEAR_OREFLAG(master->huart);
        master->huart->ErrorCode = HAL_UART_ERROR_NONE;

        rx_complete_flag = 0;
        rx_complete_length = 0;
        master->rx_length = 0;
        memset(dma_rx_buffer, 0, sizeof(dma_rx_buffer));
        HAL_UARTEx_ReceiveToIdle_DMA(master->huart, dma_rx_buffer, sizeof(dma_rx_buffer));
    }

    switch (master->state)
    {
        /*--------------------------------------------------------------------*/
        /* 空闲状态：优先处理命令队列，然后传感器轮询                              */
        /*--------------------------------------------------------------------*/
        case MODBUS_MASTER_STATE_IDLE:
        {
            /* 优先级1：检查命令队列 */
            if (master->cmd_count > 0)
            {
                modbus_cmd_t *cmd = &master->cmd_queue[master->cmd_head];
                master->current_cmd = cmd;
                cmd->status = MODBUS_CMD_STATUS_SENDING;

                /* 根据命令类型构建请求帧 */
                if (cmd->type == MODBUS_CMD_WRITE_MULTIPLE)
                {
                    master->tx_length = modbus_build_write_multiple_reg(
                        master->tx_buffer, cmd->slave_id,
                        cmd->start_addr, cmd->quantity, cmd->data);
                }
                else
                {
                    master->tx_length = modbus_build_write_single_reg(
                        master->tx_buffer, cmd->slave_id,
                        cmd->start_addr, cmd->data[0]);
                }

                /* 发送前重置 DMA 接收状态 */
                HAL_UART_AbortReceive(master->huart);
                memset(dma_rx_buffer, 0, sizeof(dma_rx_buffer));
                rx_complete_flag = 0;
                master->rx_length = 0;
                HAL_UARTEx_ReceiveToIdle_DMA(master->huart, dma_rx_buffer, sizeof(dma_rx_buffer));

                /* 发送请求帧 */
                modbus_master_send_frame(master, master->tx_buffer, master->tx_length);

                /* 切换到等待响应状态 */
                master->state = MODBUS_MASTER_STATE_WAITING_RESPONSE;
                master->timeout_start = HAL_GetTick();
                return;
            }

            /* 优先级2：传感器轮询 */
            if (master->sensor_count == 0)
            {
                return;
            }

            modbus_sensor_t *sensor = &master->sensors[master->current_slave_index];

            /* 轮询间隔控制 */
            uint32_t poll_interval = sensor->is_active ? 1000 : 5000;

            if (HAL_GetTick() - sensor->last_poll_time < poll_interval)
            {
                master->current_slave_index = (master->current_slave_index + 1) % master->sensor_count;
                break;
            }

            /* 构建读保持寄存器请求 */
            uint8_t req_buffer[8];
            modbus_build_request(req_buffer, sensor->slave_id, MODBUS_FUNC_READ_HOLDING,
                                 sensor->start_addr, sensor->quantity);

            master->tx_length = 8;
            memcpy(master->tx_buffer, req_buffer, 8);

            /* 发送前重置 DMA 接收状态 */
            HAL_UART_AbortReceive(master->huart);
            memset(dma_rx_buffer, 0, sizeof(dma_rx_buffer));
            rx_complete_flag = 0;
            master->rx_length = 0;
            HAL_UARTEx_ReceiveToIdle_DMA(master->huart, dma_rx_buffer, sizeof(dma_rx_buffer));

            /* 发送请求帧 */
            modbus_master_send_frame(master, master->tx_buffer, master->tx_length);

            /* 标记为传感器轮询模式（无命令） */
            master->current_cmd = NULL;
            master->state = MODBUS_MASTER_STATE_WAITING_RESPONSE;
            master->timeout_start = HAL_GetTick();
            break;
        }

        /*--------------------------------------------------------------------*/
        /* 等待响应状态：检查超时或接收完成                                      */
        /*--------------------------------------------------------------------*/
        case MODBUS_MASTER_STATE_WAITING_RESPONSE:
        {
            if ((HAL_GetTick() - master->timeout_start) > MODBUS_MASTER_TIMEOUT_MS)
            {
                /* 超时处理 */
                if (master->current_cmd != NULL)
                {
                    /* 命令超时，重试或失败 */
                    master->current_cmd->retry_count++;
                    if (master->current_cmd->retry_count >= MODBUS_CMD_MAX_RETRY)
                    {
                        modbus_master_finish_cmd(master, 0);
                    }
                    else
                    {
                        /* 重试：保持在当前状态，重新发送 */
                        master->state = MODBUS_MASTER_STATE_IDLE;
                    }
                }
                else
                {
                    /* 传感器轮询超时 */
                    modbus_sensor_t *sensor = &master->sensors[master->current_slave_index];
                    sensor->retry_count++;
                    sensor->last_poll_time = HAL_GetTick();

                    if (sensor->retry_count >= MODBUS_MAX_RETRY)
                    {
                        sensor->is_active = 0;
                    }

                    master->current_slave_index = (master->current_slave_index + 1) % master->sensor_count;
                    master->state = MODBUS_MASTER_STATE_IDLE;
                }
            }
            else if (modbus_master_check_rx_complete())
            {
                master->state = MODBUS_MASTER_STATE_PROCESSING;
            }
            break;
        }

        /*--------------------------------------------------------------------*/
        /* 处理状态：解析响应数据                                               */
        /*--------------------------------------------------------------------*/
        case MODBUS_MASTER_STATE_PROCESSING:
        {
            uint8_t response_valid = modbus_master_process_response(master, master->rx_buffer, master->rx_length);

            if (master->current_cmd != NULL)
            {
                /* 命令响应处理 */
                if (response_valid)
                {
                    modbus_master_finish_cmd(master, 1);
                }
                else
                {
                    master->current_cmd->retry_count++;
                    if (master->current_cmd->retry_count >= MODBUS_CMD_MAX_RETRY)
                    {
                        modbus_master_finish_cmd(master, 0);
                    }
                    else
                    {
                        master->state = MODBUS_MASTER_STATE_IDLE;
                    }
                }
            }
            else
            {
                /* 传感器轮询响应处理 */
                if (response_valid)
                {
                    modbus_sensor_t *sensor = &master->sensors[master->current_slave_index];
                    uint8_t byte_count = master->rx_buffer[2];

                    if (byte_count <= 64)
                    {
                        memcpy(sensor->data, &master->rx_buffer[3], byte_count);
                        sensor->data_length = byte_count;
                    }

                    sensor->is_active = 1;
                    sensor->retry_count = 0;
                    sensor->last_poll_time = HAL_GetTick();
                }
                else
                {
                    modbus_sensor_t *sensor = &master->sensors[master->current_slave_index];
                    sensor->retry_count++;
                    sensor->last_poll_time = HAL_GetTick();

                    if (sensor->retry_count >= MODBUS_MAX_RETRY)
                    {
                        sensor->is_active = 0;
                        sensor->retry_count = 0;
                    }
                }

                master->current_slave_index = (master->current_slave_index + 1) % master->sensor_count;
                master->state = MODBUS_MASTER_STATE_IDLE;
            }
            break;
        }

        /*--------------------------------------------------------------------*/
        /* 默认处理：未知状态恢复到空闲                                          */
        /*--------------------------------------------------------------------*/
        default:
            master->state = MODBUS_MASTER_STATE_IDLE;
            break;
    }
}

/*============================================================================*/
/*                           发送函数                                           */
/*============================================================================*/

/**
 * @brief  发送Modbus数据帧
 * @param  master: 主机结构体指针
 * @param  data:   要发送的数据缓冲区
 * @param  length: 数据长度（字节）
 * @note   本函数实现了RS485半双工通信的方向控制：
 *         1. 发送前：将DE引脚置高，使能发送模式
 *         2. 发送时：使用DMA进行数据传输，减少CPU占用
 *         3. 发送后：等待发送完成，将DE引脚置低，切换回接收模式
 *
 *         时序要求：
 *         - 发送前延时1ms，确保RS485芯片切换到发送模式
 *         - 发送后延时1ms，确保最后一个字节完全发送
 * @retval 无
 */
void modbus_master_send_frame(modbus_master_t *master, uint8_t *data, uint16_t length)
{
    /*
     * 步骤1：切换RS485到发送模式
     * DE引脚高电平 = 发送模式
     * DE引脚低电平 = 接收模式
     */
    HAL_GPIO_WritePin(UART1_CTRL_GPIO_Port, UART1_CTRL_Pin, GPIO_PIN_SET);
    HAL_Delay(1);  /* 等待RS485芯片切换完成 */

    /*
     * 步骤2：使用DMA发送数据
     * DMA发送是非阻塞的，函数会立即返回
     */
    HAL_UART_Transmit_DMA(master->huart, data, length);

    /*
     * 步骤3：等待发送完成, 加超时兜底防异常卡死
     * 先等DMA传输完成，再等UART移位寄存器发完最后一个字节
     */
    uint32_t tickstart = HAL_GetTick();
    while (HAL_UART_GetState(master->huart) == HAL_UART_STATE_BUSY_TX)
    {
        /* 等待DMA将所有数据写入UART TDR */
        if ((HAL_GetTick() - tickstart) > 10U)
        {
            break;  /* 超时退出: UART/DMA异常时避免任务无限阻塞拖垮看门狗 */
        }
    }

    /* 等待UART移位寄存器发送完成 (TC=Transmission Complete)
     * DMA完成 ≠ 发送完成，DMA完成时最后几个字节可能还在移位寄存器中
     * 必须等TC标志才能切换RS485方向，否则尾部字节会被截断 */
    tickstart = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(master->huart, UART_FLAG_TC) == RESET)
    {
        if ((HAL_GetTick() - tickstart) > 10U)
        {
            break;
        }
    }

    /*
     * 步骤4：切换RS485到接收模式
     * 确保最后一个停止位已发出后才切换方向
     */
    HAL_GPIO_WritePin(UART1_CTRL_GPIO_Port, UART1_CTRL_Pin, GPIO_PIN_RESET);
}

/*============================================================================*/
/*                           响应处理函数                                       */
/*============================================================================*/

/**
 * @brief  处理Modbus响应数据
 * @param  master: 主机结构体指针
 * @param  data:   响应数据缓冲区
 * @param  length: 数据长度
 * @retval 1: 响应有效（CRC正确，无异常）
 * @retval 0: 响应无效（长度错误、CRC错误或异常响应）
 * @note   验证内容包括：
 *         1. 最小长度检查（至少5字节：地址+功能码+数据+CRC）
 *         2. CRC校验
 *         3. 异常响应检查（功能码最高位为1表示异常）
 */
uint8_t modbus_master_process_response(modbus_master_t *master, uint8_t *data, uint16_t length)
{
    /* 检查最小长度：地址(1) + 功能码(1) + 数据(n) + CRC(2) >= 5 */
    if (length < 5)
    {
        return 0;
    }

    /* 验证CRC校验码 */
    if (!modbus_check_crc(data, length))
    {
        return 0;
    }

    /*
     * 检查是否为异常响应
     * Modbus协议规定：异常响应的功能码 = 请求功能码 + 0x80
     * 即功能码最高位为1表示异常
     * 异常响应格式：[地址][异常功能码][异常码][CRC]
     */
    if (data[1] & 0x80)
    {
        return 0;
    }

    return 1;
}

/*============================================================================*/
/*                           数据获取函数                                       */
/*============================================================================*/

/**
 * @brief  获取传感器值（第一个寄存器的值）
 * @param  sensor_index: 传感器索引（0开始）
 * @param  value:        值输出指针，用于返回16位寄存器值
 * @retval 1: 获取成功
 * @retval 0: 获取失败（索引无效、传感器离线或数据不足）
 * @note   此函数获取传感器第一个寄存器的值，适用于单值传感器（如水位传感器）
 */
uint8_t modbus_master_get_sensor_value(uint8_t sensor_index, uint16_t *value)
{
    /* 参数有效性检查 */
    if (value == NULL)
    {
        return 0;
    }

    /* 索引范围检查 */
    if (sensor_index >= sensor_master.sensor_count)
    {
        return 0;
    }

    modbus_sensor_t *sensor = &sensor_master.sensors[sensor_index];

    /* 检查传感器在线状态和数据长度 */
    if (!sensor->is_active || sensor->data_length < 2)
    {
        return 0;
    }

    /* 获取第一个寄存器的值 */
    *value = modbus_master_get_register_value(sensor_index, 0);
    return 1;
}

/**
 * @brief  获取传感器原始数据缓冲区
 * @param  sensor_index: 传感器索引（0开始）
 * @retval 非NULL: 传感器数据缓冲区指针
 * @retval NULL: 索引无效
 * @note   返回的缓冲区包含所有读取到的寄存器数据
 *         数据格式：大端序，每2字节为一个16位寄存器值
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
 * @brief  获取传感器数据长度
 * @param  sensor_index: 传感器索引（0开始）
 * @retval 数据长度（字节数）
 * @note   返回值除以2即为寄存器数量
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
 * @brief  检查传感器是否在线
 * @param  sensor_index: 传感器索引（0开始）
 * @retval 1: 在线（最近通信成功）
 * @retval 0: 离线（连续多次通信失败）
 * @note   传感器在连续MODBUS_MAX_RETRY次通信失败后被标记为离线
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
 * @brief  获取指定寄存器的值
 * @param  sensor_index:   传感器索引（0开始）
 * @param  register_index: 寄存器索引（0开始，相对于传感器配置的起始地址）
 * @retval 寄存器值（16位无符号整数）
 * @retval 0: 索引无效或数据不足
 * @note   例如：传感器配置读取地址0x0000开始的3个寄存器
 *         - register_index=0 返回地址0x0000的值
 *         - register_index=1 返回地址0x0001的值
 *         - register_index=2 返回地址0x0002的值
 *
 *         数据存储格式为大端序（高字节在前）
 */
uint16_t modbus_master_get_register_value(uint8_t sensor_index, uint8_t register_index)
{
    /* 索引范围检查 */
    if (sensor_index >= sensor_master.sensor_count)
    {
        return 0;
    }

    modbus_sensor_t *sensor = &sensor_master.sensors[sensor_index];

    /* 检查数据长度是否足够 */
    if ((register_index * 2 + 1) >= sensor->data_length)
    {
        return 0;
    }

    /*
     * 从数据缓冲区提取16位寄存器值
     * 数据格式：大端序（高字节在前，低字节在后）
     */
    uint16_t value = (sensor->data[register_index * 2] << 8) |      /* 高字节 */
                     sensor->data[register_index * 2 + 1];          /* 低字节 */

    return value;
}

/*============================================================================*/
/*                           命令队列函数                                       */
/*============================================================================*/

/**
 * @brief  完成当前命令
 * @param  master: 主机结构体指针
 * @param  success: 1-成功 0-失败
 */
static void modbus_master_finish_cmd(modbus_master_t *master, uint8_t success)
{
    if (master->current_cmd == NULL)
    {
        master->state = MODBUS_MASTER_STATE_IDLE;
        return;
    }

    master->current_cmd->status = success ? MODBUS_CMD_STATUS_SUCCESS : MODBUS_CMD_STATUS_FAILED;

    /* 调用回调函数 */
    if (master->current_cmd->callback != NULL)
    {
        master->current_cmd->callback(success ? 1 : 0);
    }

    /* 移出队列 */
    master->cmd_head = (master->cmd_head + 1) % MODBUS_CMD_QUEUE_SIZE;
    master->cmd_count--;
    master->current_cmd = NULL;
    master->state = MODBUS_MASTER_STATE_IDLE;
}

/**
 * @brief  提交写单个寄存器命令
 * @param  master: 主机结构体指针
 * @param  slave_id: 从机ID (1-247)
 * @param  reg_addr: 寄存器地址
 * @param  value: 写入值
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败-队列满)
 */
uint8_t modbus_master_write_reg(modbus_master_t *master, uint8_t slave_id,
                                 uint16_t reg_addr, uint16_t value,
                                 void (*callback)(uint8_t result))
{
    /* 检查队列是否已满 */
    if (master->cmd_count >= MODBUS_CMD_QUEUE_SIZE)
    {
        return 0xFF;
    }

    /* 获取队列尾部位置 */
    uint8_t index = master->cmd_tail;
    modbus_cmd_t *cmd = &master->cmd_queue[index];

    /* 填充命令信息 */
    cmd->type = MODBUS_CMD_WRITE_SINGLE;
    cmd->status = MODBUS_CMD_STATUS_PENDING;
    cmd->slave_id = slave_id;
    cmd->start_addr = reg_addr;
    cmd->quantity = 1;
    cmd->data[0] = value;
    cmd->retry_count = 0;
    cmd->callback = callback;

    /* 更新队列尾指针 */
    master->cmd_tail = (master->cmd_tail + 1) % MODBUS_CMD_QUEUE_SIZE;
    master->cmd_count++;

    return index;
}

/**
 * @brief  提交写多个寄存器命令
 * @param  master: 主机结构体指针
 * @param  slave_id: 从机ID (1-247)
 * @param  start_addr: 起始地址
 * @param  quantity: 寄存器数量 (最多16个)
 * @param  data: 写入数据数组
 * @param  callback: 完成回调函数 (可选，传NULL)
 * @retval 命令索引 (>=0成功，0xFF失败-队列满)
 */
uint8_t modbus_master_write_regs(modbus_master_t *master, uint8_t slave_id,
                                  uint16_t start_addr, uint16_t quantity,
                                  const uint16_t *data,
                                  void (*callback)(uint8_t result))
{
    /* 检查队列是否已满 */
    if (master->cmd_count >= MODBUS_CMD_QUEUE_SIZE)
    {
        return 0xFF;
    }

    /* 检查寄存器数量 */
    if (quantity == 0 || quantity > 16)
    {
        return 0xFF;
    }

    /* 获取队列尾部位置 */
    uint8_t index = master->cmd_tail;
    modbus_cmd_t *cmd = &master->cmd_queue[index];

    /* 填充命令信息 */
    cmd->type = MODBUS_CMD_WRITE_MULTIPLE;
    cmd->status = MODBUS_CMD_STATUS_PENDING;
    cmd->slave_id = slave_id;
    cmd->start_addr = start_addr;
    cmd->quantity = quantity;
    cmd->retry_count = 0;
    cmd->callback = callback;

    /* 复制数据 */
    for (uint16_t i = 0; i < quantity; i++)
    {
        cmd->data[i] = data[i];
    }

    /* 更新队列尾指针 */
    master->cmd_tail = (master->cmd_tail + 1) % MODBUS_CMD_QUEUE_SIZE;
    master->cmd_count++;

    return index;
}

/**
 * @brief  检查命令是否完成
 * @param  master: 主机结构体指针
 * @param  cmd_index: 命令索引 (由提交函数返回)
 * @retval 命令状态 (MODBUS_CMD_STATUS_*)
 */
modbus_cmd_status_t modbus_master_get_cmd_status(modbus_master_t *master, uint8_t cmd_index)
{
    if (cmd_index >= MODBUS_CMD_QUEUE_SIZE)
    {
        return MODBUS_CMD_STATUS_FAILED;
    }

    return master->cmd_queue[cmd_index].status;
}

/**
 * @brief  获取命令队列中待处理命令数量
 * @param  master: 主机结构体指针
 * @retval 待处理命令数量
 */
uint8_t modbus_master_get_pending_cmd_count(modbus_master_t *master)
{
    return master->cmd_count;
}
