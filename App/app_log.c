#include "app_log.h"

void app_log_init(void)
{
log_manager_init(NULL);

log_write(LOG_TYPE_SYSTEM, "init");
log_write(LOG_TYPE_USER, "admin");
log_write(LOG_TYPE_ALARM, "123");

//log_cleanup_all(30);

}