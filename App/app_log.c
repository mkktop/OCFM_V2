#include "app_log.h"

void app_log_init(void)
{
    /* 1. 历史记录系统初始化 */
    if (data_recorder_init(NULL) != FILE_OK) {
        printf("recorder init failed!\r\n");
        return;
    }
    
    /* 2. 记录几条测试数据 */
    data_record_flow(1.234f, 0.056789f, 12345.678, 3600, 25.5f, 0);
    data_record_flow(1.235f, 0.056890f, 12346.234, 7200, 25.6f, 0);
    data_record_flow(1.500f, 0.060000f, 12350.000, 10800, 25.8f, 1);  // 带报警
    printf("Data recorded!\r\n");
}