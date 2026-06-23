/**
 * @file modbus_slave.c
 * @brief Modbus从机实现 - 响应用户指令
 * @details 通过串口2连接用户设备，使用DMA+空闲中断接收，响应Modbus指令
 *          使用地址映射表，将Modbus地址映射到紧凑数组索引
 */

#include "modbus_slave.h"
#include "modbus.h"
#include "global.h"
#include <string.h>

/*============================================================================*/
/*                           地址映射表                                         */
/*============================================================================*/

/**
 * @brief 映射表项
 */
typedef struct {
    uint16_t reg_addr;   /**< Modbus寄存器地址 */
    uint8_t  index;      /**< 紧凑数组索引 */
    uint8_t  writable;   /**< 1=可写(0x06/0x10允许), 0=只读 */
} reg_map_t;

/**
 * @brief 寄存器地址映射表
 * @note 将非连续的Modbus地址映射到连续的数组索引
 *       表项按reg_addr升序排列，用于二分查找
 */
static const reg_map_t reg_map[] = {
    /* 传感器实时数据区 (0x0001-0x0005): 只读 */
    {REG_WUWEI,            0, 0},
    {REG_DISTANCE,         1, 0},
    {REG_TEMPERATURE,      2, 0},
    {REG_INSTANT_FLOW,     3, 0},   /* float 占2个索引: 3,4 (只读实时) */
    {0x0005,               4, 0},   /* 瞬时流量低字 (只读) */
    /* 累计流量区 (0x0006-0x0009): 可写 (主站可设基准值) */
    {REG_SUM_FLOW,         5, 1},   /* double 占4个索引: 5,6,7,8 */
    {0x0007,               6, 1},   /* 累计流量字2 */
    {0x0008,               7, 1},   /* 累计流量字3 */
    {0x0009,               8, 1},   /* 累计流量字4 */
    /* 继电器状态区 (0x000A-0x000D): 只读 (GPIO读回) */
    {REG_RELAY1_STATUS,    9,  0},
    {REG_RELAY2_STATUS,    10, 0},
    {REG_RELAY3_STATUS,    11, 0},
    {REG_RELAY4_STATUS,    12, 0},
    /* 报警值区 (0x000E-0x0019): 可写 */
    {REG_AH,               13, 1},  /* float 占2个: 13,14 */
    {0x000F,               14, 1},  /* AH低字 */
    {REG_DH,               15, 1},  /* float 占2个: 15,16 */
    {0x0011,               16, 1},  /* DH低字 */
    {REG_AL,               17, 1},  /* float 占2个: 17,18 */
    {0x0013,               18, 1},  /* AL低字 */
    {REG_DL,               19, 1},  /* float 占2个: 19,20 */
    {0x0015,               20, 1},  /* DL低字 */
    {REG_AAH,              21, 1},  /* float 占2个: 21,22 */
    {0x0017,               22, 1},  /* AAH低字 */
    {REG_AAL,              23, 1},  /* float 占2个: 23,24 */
    {0x0019,               24, 1},  /* AAL低字 */
    /* 累计计量时间区 (0x001A-0x001B, uint32): 只读输出 */
    {REG_TOTAL_TIME,       63, 0},  /* 累计计量时间(秒) 占2个: 63,64 */
    {0x001B,               64, 0},  /* 累计计量时间低字 */
    /* 传感器参数区 (0x0065-0x006F): 可写 */
    {REG_RANGE_MAX,        25, 1},
    {REG_HEIGHT,           26, 1},
    {REG_L1,               27, 1},
    {REG_L2,               28, 1},
    {REG_L3,               29, 1},
    {REG_L4,               30, 1},
    {REG_L5,               31, 1},
    {REG_L6,               32, 1},
    {REG_ADDRESS,          33, 1},
    {REG_BAUDE_RATE,       34, 1},
    {REG_STOP_BITS,        35, 1},
    /* 从机参数区 (0x0101-0x010E): 可写 */
    {REG_CANALS__TYPE,     36, 1},
    {REG_CHANNEL_ID,       37, 1},
    {REG_INSTANT_UNIT,     38, 1},
    {REG_SUM_POINT,        39, 1},
    {REG_RANGE_4MA,        40, 1},  /* float 占2个: 40,41 */
    {0x0106,               41, 1},  /* 4mA量程低字 */
    {REG_RANGE_20MA,       42, 1},  /* float 占2个: 42,43 */
    {0x0108,               43, 1},  /* 20mA量程低字 */
    {REG_CHANNEL_WIDTH,    57, 1},
    {REG_WEIR_HEIGHT,      58, 1},
    {REG_WATER_LEVEL_UP,   59, 1},  /* float 占2个: 59,60 */
    {0x010C,               60, 1},  /* 水位上限低字 */
    {REG_WATER_LEVEL_DOWN, 61, 1},  /* float 占2个: 61,62 */
    {0x010E,               62, 1},  /* 水位下限低字 */
    /* RTC时间设置区 (0x0200-0x0206): 可写 */
    {REG_RTC_YEAR,         50, 1},
    {REG_RTC_MONTH,        51, 1},
    {REG_RTC_DAY,          52, 1},
    {REG_RTC_HOUR,         53, 1},
    {REG_RTC_MINUTE,       54, 1},
    {REG_RTC_SECOND,       55, 1},
    {REG_RTC_WEEKDAY,      56, 1},
    /* 出厂校准区 (0x1001-0x1006): 可写 */
    {REG_ANTENNA_TYPE,     44, 1},
    {REG_DIS_OFFSET,       45, 1},
    {REG_CALIBRATION_4MA,  46, 1},
    {REG_CALIBRATION_20MA, 47, 1},
    {REG_FACTORY_SETTING,  48, 1},
    {REG_CLEAR_TOTAL,      49, 1},
};

#define REG_MAP_SIZE    (sizeof(reg_map) / sizeof(reg_map[0]))
#define REG_ARRAY_SIZE  66  /* 映射后的紧凑数组大小 (含累计计量时间 63,64) */

/**
 * @brief 保持寄存器紧凑数组
 * @note 通过地址映射表访问，节省RAM
 */
static uint16_t holding_registers[REG_ARRAY_SIZE];

/**
 * @brief Modbus地址转映射表项 (二分查找)
 * @param addr Modbus寄存器地址
 * @return 表项指针, NULL表示未找到
 */
static const reg_map_t *reg_addr_to_entry(uint16_t addr)
{
    int16_t left = 0;
    int16_t right = REG_MAP_SIZE - 1;

    while (left <= right)
    {
        int16_t mid = (left + right) / 2;
        if (reg_map[mid].reg_addr == addr)
        {
            return &reg_map[mid];
        }
        else if (reg_map[mid].reg_addr < addr)
        {
            left = mid + 1;
        }
        else
        {
            right = mid - 1;
        }
    }
    return NULL;
}

/**
 * @brief Modbus地址转数组索引 (二分查找)
 * @param addr Modbus寄存器地址
 * @return 数组索引，-1表示未找到
 * @note 只返回起始地址的索引，后续寄存器索引为+1/+2/+3
 */
static int16_t reg_addr_to_index(uint16_t addr)
{
    const reg_map_t *e = reg_addr_to_entry(addr);
    return (e != NULL) ? e->index : -1;
}

/**
 * @brief 查询寄存器地址是否可写
 * @param addr Modbus寄存器地址
 * @retval 1 可写 (0x06/0x10 允许写入)
 * @retval 0 只读或未映射 (写入应返回 ILLEGAL_DATA_ADDR)
 */
uint8_t modbus_slave_addr_writable(uint16_t addr)
{
    const reg_map_t *e = reg_addr_to_entry(addr);
    return (e != NULL) ? e->writable : 0;
}

/**
 * @brief 全局Modbus从机实例
 */
modbus_slave_t sensor_slave;

/**
 * @brief DMA接收缓冲区
 * @note 用于HAL_UARTEx_ReceiveToIdle_DMA函数的接收缓冲区
 */
static uint8_t dma_rx_buffer[MODBUS_SLAVE_BUF_SIZE];

/**
 * @brief 接收完成标志
 * @note 当空闲中断触发时置1，表示一帧数据接收完成
 */
static volatile uint8_t rx_complete_flag = 0;

/**
 * @brief 接收数据长度
 * @note 记录最近一次接收到的数据字节数
 */
static volatile uint16_t rx_complete_length = 0;

/**
 * @brief 写入回调函数指针
 */
static modbus_write_callback_t write_callback = NULL;

/**
 * @brief 注册写入回调函数
 * @param callback 回调函数指针
 */
void modbus_slave_set_write_callback(modbus_write_callback_t callback)
{
    write_callback = callback;
}

/**
 * @brief 初始化Modbus从机
 * @param slave 从机结构体指针
 * @param huart 串口句柄指针
 * @note 初始化串口、寄存器数据，并启动DMA+空闲中断接收
 */
void modbus_slave_init(modbus_slave_t *slave, UART_HandleTypeDef *huart)
{
    /* 清零结构体 */
    memset(slave, 0, sizeof(modbus_slave_t));

    /* 设置串口句柄 */
    slave->huart = huart;

    /* 设置初始状态 */
    slave->state = MODBUS_SLAVE_STATE_IDLE;

    /* 设置默认从机ID */
    slave->slave_id = MODBUS_SLAVE_ID;

    /* 清零寄存器 */
    memset(holding_registers, 0, sizeof(holding_registers));

    modbus_slave_restart_rx(slave, huart);
}

void modbus_slave_restart_rx(modbus_slave_t *slave, UART_HandleTypeDef *huart)
{
    slave->huart = huart;
    slave->rx_length = 0;
    slave->state = MODBUS_SLAVE_STATE_IDLE;

    /* 清零接收标志 */
    rx_complete_flag = 0;
    rx_complete_length = 0;

    memset(dma_rx_buffer, 0, sizeof(dma_rx_buffer));

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
 * @brief 设置从机ID
 * @param slave 从机结构体指针
 * @param id 从机ID (1-247)
 */
void modbus_slave_set_id(modbus_slave_t *slave, uint8_t id)
{
    if (id > 0 && id <= 247)
    {
        slave->slave_id = id;
    }
}

/**
 * @brief 串口空闲中断回调函数
 * @param huart 触发中断的串口句柄
 * @param size 接收到的数据字节数
 * @note 由HAL_UARTEx_RxEventCallback()调用，处理UART2的空闲中断
 *       将DMA缓冲区数据复制到从机接收缓冲区，重新启动DMA接收
 */
void modbus_slave_rx_idle_callback(UART_HandleTypeDef *huart, uint16_t size)
{
    /* 检查是否为Modbus从机使用的串口 */
    if (huart == sensor_slave.huart)
    {
        /* 将DMA缓冲区数据复制到从机接收缓冲区 */
        memcpy(sensor_slave.rx_buffer, dma_rx_buffer, size);
        sensor_slave.rx_length = size;

        /* 记录接收完成信息 */
        rx_complete_length = size;
        rx_complete_flag = 1;
        sensor_slave.state = MODBUS_SLAVE_STATE_PROCESSING;

        /*
         * 重置DMA接收状态并重新启动
         * 注意：先中止当前DMA接收，清除缓冲区，再重新启动，避免旧数据残留
         */
        HAL_UART_AbortReceive(huart);
        memset(dma_rx_buffer, 0, sizeof(dma_rx_buffer));
        HAL_UARTEx_ReceiveToIdle_DMA(huart, dma_rx_buffer, sizeof(dma_rx_buffer));
    }
}

/**
 * @brief Modbus从机任务处理
 * @param slave 从机结构体指针
 * @note 非阻塞方式，需在FreeRTOS任务中周期性调用
 *       检查DMA+空闲中断是否收到完整帧，处理并响应
 */
void modbus_slave_task(modbus_slave_t *slave)
{
    /*
     * UART错误自动恢复
     * DMA模式下，任何UART错误(FE/ORE/NE)都会导致HAL中止DMA接收，
     * 使RxState变为READY。此处检测到异常后重新启动DMA+空闲中断接收。
     */
    if (slave->huart != NULL && slave->huart->RxState != HAL_UART_STATE_BUSY_RX)
    {
        __HAL_UART_CLEAR_OREFLAG(slave->huart);
        slave->huart->ErrorCode = HAL_UART_ERROR_NONE;

        rx_complete_flag = 0;
        rx_complete_length = 0;
        slave->rx_length = 0;
        memset(dma_rx_buffer, 0, sizeof(dma_rx_buffer));
        HAL_UARTEx_ReceiveToIdle_DMA(slave->huart, dma_rx_buffer, sizeof(dma_rx_buffer));
    }

    /* 检查是否收到完整的Modbus请求帧 */
    if (!rx_complete_flag)
    {
        return;
    }
    rx_complete_flag = 0;

    /* 更新状态 */
    slave->state = MODBUS_SLAVE_STATE_PROCESSING;

    /* 处理接收到的数据 */
    uint16_t response_len = modbus_slave_process(slave,
                                                  slave->rx_buffer,
                                                  slave->rx_length);

    /* 广播地址(0)按Modbus协议不返回响应 */
    uint8_t is_broadcast = (slave->rx_length > 0 && slave->rx_buffer[0] == 0) ? 1 : 0;

    /* 如果有响应数据且非广播，发送响应 */
    if (response_len > 0 && !is_broadcast)
    {
        slave->state = MODBUS_SLAVE_STATE_SENDING;
        modbus_slave_send(slave, slave->tx_buffer, response_len);
    }

    /* 清空接收缓冲区，恢复空闲状态 */
    slave->rx_length = 0;
    slave->state = MODBUS_SLAVE_STATE_IDLE;
}

/**
 * @brief 处理接收到的数据
 * @param slave 从机结构体指针
 * @param data 接收到的数据
 * @param length 数据长度
 * @return 处理后的响应长度，0表示无需响应
 */
uint16_t modbus_slave_process(modbus_slave_t *slave, uint8_t *data, uint16_t length)
{
    /* 检查最小帧长度 (地址+功能码+CRC = 4字节) */
    if (length < 4)
    {
        return 0;
    }
    
    /* 验证CRC */
    if (!modbus_check_crc(data, length))
    {
        return 0;
    }
    
    /* 检查从机地址是否匹配（含广播地址0） */
    if (data[0] != slave->slave_id && data[0] != 0)
    {
        return 0;
    }

    /* 获取功能码 */
    uint8_t function_code = data[1];
    
    /* 根据功能码处理请求 */
    switch (function_code)
    {
        case MODBUS_FUNC_READ_HOLDING:
        {
            /* 读保持寄存器 (0x03) */
            uint16_t start_addr = (data[2] << 8) | data[3];
            uint16_t quantity = (data[4] << 8) | data[5];
            return modbus_slave_read_holding_registers(slave, start_addr, quantity, slave->tx_buffer);
        }
        
        case MODBUS_FUNC_WRITE_SINGLE_REG:
        {
            /* 写单个寄存器 (0x06) */
            uint16_t register_addr = (data[2] << 8) | data[3];
            uint16_t value = (data[4] << 8) | data[5];
            return modbus_slave_write_single_register(slave, register_addr, value, slave->tx_buffer);
        }
        
        case MODBUS_FUNC_WRITE_MULTIPLE_REG:
        {
            /* 写多个寄存器 (0x10) */
            uint16_t start_addr = (data[2] << 8) | data[3];
            uint16_t quantity = (data[4] << 8) | data[5];

            /*
             * 帧完整性校验 (防越界读 rx_buffer):
             * 0x10请求帧 = 地址(1)+功能码(1)+起始地址(2)+数量(2)+字节数(1)
             *              +数据(quantity*2)+CRC(2), 最小9字节
             * 必须校验 byte_count(data[6]) == quantity*2 且帧长足以容纳数据+CRC,
             * 否则恶意/畸形帧(quantity大但实际数据短)会让 write_multiple_registers
             * 内部循环从 &data[7] 起越界读取 rx_buffer 之外的内存。
             * 用32位比较防止 quantity*2 截断成uint8_t后误判。
             */
            if (length < 9 ||
                (uint32_t)quantity * 2u != (uint32_t)data[6] ||
                length < (uint16_t)(7u + (uint32_t)data[6] + 2u))
            {
                return modbus_slave_build_exception(slave, function_code,
                                                     MODBUS_EX_ILLEGAL_DATA_VALUE,
                                                     slave->tx_buffer);
            }

            return modbus_slave_write_multiple_registers(slave, start_addr, quantity, &data[7], slave->tx_buffer);
        }
        
        default:
        {
            /* 不支持的功能码，返回异常响应 */
            return modbus_slave_build_exception(slave, function_code,
                                                 MODBUS_EX_ILLEGAL_FUNCTION,
                                                 slave->tx_buffer);
        }
    }
}

/**
 * @brief 读保持寄存器 (功能码0x03)
 * @param slave 从机结构体指针
 * @param start_addr 起始地址
 * @param quantity 寄存器数量
 * @param response 响应缓冲区
 * @return 响应长度
 */
uint16_t modbus_slave_read_holding_registers(modbus_slave_t *slave,
                                               uint16_t start_addr,
                                               uint16_t quantity,
                                               uint8_t *response)
{
    /* 检查数量范围 (最大125个寄存器) */
    if (quantity < 1 || quantity > 125)
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_READ_HOLDING,
                                             MODBUS_EX_ILLEGAL_DATA_VALUE, response);
    }

    /* 构建响应帧 */
    response[0] = slave->slave_id;              /* 从机地址 */
    response[1] = MODBUS_FUNC_READ_HOLDING;     /* 功能码 */
    response[2] = quantity * 2;                 /* 字节数 */

    /* 填充寄存器数据 (大端格式) */
    for (uint16_t i = 0; i < quantity; i++)
    {
        uint16_t addr = start_addr + i;
        int16_t idx = reg_addr_to_index(addr);
        uint16_t value = (idx >= 0) ? holding_registers[idx] : 0;

        response[3 + i * 2] = value >> 8;       /* 高字节 */
        response[3 + i * 2 + 1] = value & 0xFF; /* 低字节 */
    }

    /* 计算CRC */
    uint16_t crc = modbus_crc16(response, 3 + quantity * 2);
    response[3 + quantity * 2] = crc & 0xFF;        /* CRC低字节 */
    response[3 + quantity * 2 + 1] = crc >> 8;      /* CRC高字节 */

    /* 返回响应长度 */
    return 5 + quantity * 2;
}

/**
 * @brief 写单个寄存器 (功能码0x06)
 * @param slave 从机结构体指针
 * @param register_addr 寄存器地址
 * @param value 值
 * @param response 响应缓冲区
 * @return 响应长度
 */
uint16_t modbus_slave_write_single_register(modbus_slave_t *slave,
                                              uint16_t register_addr,
                                              uint16_t value,
                                              uint8_t *response)
{
    /* 检查地址是否有效 */
    int16_t idx = reg_addr_to_index(register_addr);
    if (idx < 0)
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_WRITE_SINGLE_REG,
                                             MODBUS_EX_ILLEGAL_DATA_ADDR, response);
    }

    /* 只读寄存器拒绝写入 (Modbus标准: 返回非法数据地址) */
    if (!modbus_slave_addr_writable(register_addr))
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_WRITE_SINGLE_REG,
                                             MODBUS_EX_ILLEGAL_DATA_ADDR, response);
    }

    /* 写入寄存器 */
    holding_registers[idx] = value;

    /* 触发写入回调 */
    if (write_callback != NULL)
    {
        write_callback(register_addr, 1);
    }

    /* 构建响应帧 (原样返回请求) */
    response[0] = slave->slave_id;
    response[1] = MODBUS_FUNC_WRITE_SINGLE_REG;
    response[2] = register_addr >> 8;
    response[3] = register_addr & 0xFF;
    response[4] = value >> 8;
    response[5] = value & 0xFF;
    
    /* 计算CRC */
    uint16_t crc = modbus_crc16(response, 6);
    response[6] = crc & 0xFF;
    response[7] = crc >> 8;
    
    return 8;
}

/**
 * @brief 写多个寄存器 (功能码0x10)
 * @param slave 从机结构体指针
 * @param start_addr 起始地址
 * @param quantity 寄存器数量
 * @param data 数据
 * @param response 响应缓冲区
 * @return 响应长度
 */
uint16_t modbus_slave_write_multiple_registers(modbus_slave_t *slave,
                                                 uint16_t start_addr,
                                                 uint16_t quantity,
                                                 uint8_t *data,
                                                 uint8_t *response)
{
    /* 检查数量范围 */
    if (quantity < 1 || quantity > 123)
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_WRITE_MULTIPLE_REG,
                                             MODBUS_EX_ILLEGAL_DATA_VALUE, response);
    }

    /* 检查起始地址是否有效 */
    int16_t idx = reg_addr_to_index(start_addr);
    if (idx < 0)
    {
        return modbus_slave_build_exception(slave, MODBUS_FUNC_WRITE_MULTIPLE_REG,
                                             MODBUS_EX_ILLEGAL_DATA_ADDR, response);
    }

    /* 预扫描整个写入范围: 任一只读/未映射地址则整体拒绝 (原子语义, 不部分写入) */
    for (uint16_t i = 0; i < quantity; i++)
    {
        if (!modbus_slave_addr_writable((uint16_t)(start_addr + i)))
        {
            return modbus_slave_build_exception(slave, MODBUS_FUNC_WRITE_MULTIPLE_REG,
                                                 MODBUS_EX_ILLEGAL_DATA_ADDR, response);
        }
    }

    /* 写入寄存器 (逐个映射) */
    for (uint16_t i = 0; i < quantity; i++)
    {
        uint16_t addr = start_addr + i;
        int16_t reg_idx = reg_addr_to_index(addr);
        if (reg_idx >= 0)
        {
            holding_registers[reg_idx] = (data[i * 2] << 8) | data[i * 2 + 1];
        }
    }

    /* 触发写入回调 (整次操作只触发一次) */
    if (write_callback != NULL)
    {
        write_callback(start_addr, quantity);
    }

    /* 构建响应帧 */
    response[0] = slave->slave_id;
    response[1] = MODBUS_FUNC_WRITE_MULTIPLE_REG;
    response[2] = start_addr >> 8;
    response[3] = start_addr & 0xFF;
    response[4] = quantity >> 8;
    response[5] = quantity & 0xFF;
    
    /* 计算CRC */
    uint16_t crc = modbus_crc16(response, 6);
    response[6] = crc & 0xFF;
    response[7] = crc >> 8;
    
    return 8;
}

/**
 * @brief 构建异常响应
 * @param slave 从机结构体指针
 * @param function_code 功能码
 * @param exception_code 异常码
 * @param response 响应缓冲区
 * @return 响应长度
 */
uint16_t modbus_slave_build_exception(modbus_slave_t *slave,
                                        uint8_t function_code,
                                        uint8_t exception_code,
                                        uint8_t *response)
{
    /* 构建异常响应帧 */
    response[0] = slave->slave_id;
    response[1] = function_code | 0x80;     /* 功能码最高位置1 */
    response[2] = exception_code;
    
    /* 计算CRC */
    uint16_t crc = modbus_crc16(response, 3);
    response[3] = crc & 0xFF;
    response[4] = crc >> 8;
    
    return 5;
}

/**
 * @brief 发送响应数据
 * @param slave 从机结构体指针
 * @param data 数据
 * @param length 长度
 * @note 使用DMA发送，发送前后切换RS485方向控制引脚
 */
void modbus_slave_send(modbus_slave_t *slave, uint8_t *data, uint16_t length)
{
    /*
     * 切换RS485到发送模式
     * UART2_CTRL引脚高电平 = 发送模式
     */
    HAL_GPIO_WritePin(UART2_CTRL_GPIO_Port, UART2_CTRL_Pin, GPIO_PIN_SET);
    HAL_Delay(1);

    /* 使用DMA发送数据 */
    HAL_UART_Transmit_DMA(slave->huart, data, length);

    /* 等待DMA传输完成 */
    while (HAL_UART_GetState(slave->huart) == HAL_UART_STATE_BUSY_TX)
    {
    }

    /* 等待UART移位寄存器发送完成，防止尾部字节截断 */
    while (__HAL_UART_GET_FLAG(slave->huart, UART_FLAG_TC) == RESET)
    {
    }

    /*
     * 切换RS485到接收模式
     */
    HAL_GPIO_WritePin(UART2_CTRL_GPIO_Port, UART2_CTRL_Pin, GPIO_PIN_RESET);
}

/**
 * @brief 获取保持寄存器值
 * @param addr 寄存器地址
 * @return 寄存器值
 */
uint16_t modbus_slave_get_holding_register(uint16_t addr)
{
    int16_t idx = reg_addr_to_index(addr);
    if (idx >= 0)
    {
        return holding_registers[idx];
    }
    return 0;
}

/**
 * @brief 设置保持寄存器值
 * @param addr 寄存器地址
 * @param value 值
 */
void modbus_slave_set_holding_register(uint16_t addr, uint16_t value)
{
    int16_t idx = reg_addr_to_index(addr);
    if (idx >= 0)
    {
        holding_registers[idx] = value;
    }
}

/**
 * @brief 设置32位无符号整数 (占用2个寄存器)
 * @param addr 起始寄存器地址
 * @param value 32位值
 * @note 大端格式: 高位寄存器在前
 */
void modbus_slave_set_uint32(uint16_t addr, uint32_t value)
{
    int16_t idx = reg_addr_to_index(addr);
    if (idx >= 0)
    {
        holding_registers[idx] = (uint16_t)(value >> 16);
        holding_registers[idx + 1] = (uint16_t)(value & 0xFFFF);
    }
}

/**
 * @brief 获取32位无符号整数
 * @param addr 起始寄存器地址
 * @return 32位值
 */
uint32_t modbus_slave_get_uint32(uint16_t addr)
{
    int16_t idx = reg_addr_to_index(addr);
    if (idx >= 0)
    {
        return ((uint32_t)holding_registers[idx] << 16) |
               (uint32_t)holding_registers[idx + 1];
    }
    return 0;
}

/**
 * @brief 设置32位有符号整数 (占用2个寄存器)
 * @param addr 起始寄存器地址
 * @param value 32位值
 */
void modbus_slave_set_int32(uint16_t addr, int32_t value)
{
    modbus_slave_set_uint32(addr, (uint32_t)value);
}

/**
 * @brief 获取32位有符号整数
 * @param addr 起始寄存器地址
 * @return 32位值
 */
int32_t modbus_slave_get_int32(uint16_t addr)
{
    return (int32_t)modbus_slave_get_uint32(addr);
}

/**
 * @brief 设置浮点数 (占用2个寄存器)
 * @param addr 起始寄存器地址
 * @param value 浮点数值
 * @note IEEE 754格式存储，大端模式
 */
void modbus_slave_set_float(uint16_t addr, float value)
{
    typedef union {
        float f;
        uint32_t u;
    } float_union_t;

    float_union_t fu;
    fu.f = value;

    modbus_slave_set_uint32(addr, fu.u);
}

/**
 * @brief 获取浮点数
 * @param addr 起始寄存器地址
 * @return 浮点数值
 */
float modbus_slave_get_float(uint16_t addr)
{
    typedef union {
        float f;
        uint32_t u;
    } float_union_t;

    float_union_t fu;
    fu.u = modbus_slave_get_uint32(addr);

    return fu.f;
}

/**
 * @brief 设置双精度浮点数 (占用4个寄存器)
 * @param addr 起始寄存器地址
 * @param value 双精度浮点数值
 * @note IEEE 754格式存储，大端模式
 */
void modbus_slave_set_double(uint16_t addr, double value)
{
    int16_t idx = reg_addr_to_index(addr);
    if (idx >= 0)
    {
        typedef union {
            double d;
            uint64_t u;
        } double_union_t;

        double_union_t du;
        du.d = value;

        holding_registers[idx] = (uint16_t)(du.u >> 48);
        holding_registers[idx + 1] = (uint16_t)(du.u >> 32);
        holding_registers[idx + 2] = (uint16_t)(du.u >> 16);
        holding_registers[idx + 3] = (uint16_t)(du.u & 0xFFFF);
    }
}

/**
 * @brief 获取双精度浮点数
 * @param addr 起始寄存器地址
 * @return 双精度浮点数值
 */
double modbus_slave_get_double(uint16_t addr)
{
    int16_t idx = reg_addr_to_index(addr);
    if (idx >= 0)
    {
        typedef union {
            double d;
            uint64_t u;
        } double_union_t;

        double_union_t du;
        du.u = ((uint64_t)holding_registers[idx] << 48) |
               ((uint64_t)holding_registers[idx + 1] << 32) |
               ((uint64_t)holding_registers[idx + 2] << 16) |
               ((uint64_t)holding_registers[idx + 3]);

        return du.d;
    }
    return 0.0;
}
