#include "app_log.h"

void app_log_init(void)
{
    data_recorder_init(NULL);
    data_record_flow(
    1.234f,           // water_level 水位(m)
    0.567f,           // instant_flow 瞬时流量(m?/s)
    12345.67,         // total_flow 累计流量(m?)
    3600,             // total_time 累计时间(秒)
    25.5f,            // temperature 温度(°C)
    0x0000            // flags 标志位
);
    data_record_flow(
    3.2334f,           // water_level 水位(m)
    2.567f,           // instant_flow 瞬时流量(m?/s)
    12345.67,         // total_flow 累计流量(m?)
    3600,             // total_time 累计时间(秒)
    27.5f,            // temperature 温度(°C)
    0x0000            // flags 标志位
);


    log_manager_init(NULL);
    log_write(LOG_TYPE_SYSTEM, "init");
    log_write(LOG_TYPE_USER, "admin");
    log_write(LOG_TYPE_ALARM, "123");

//log_cleanup_all(30);

}