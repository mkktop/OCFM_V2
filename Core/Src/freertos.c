/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "global.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "demos/lv_demos.h"
#include "app_log.h"
#include "rtc_time.h"
#include "app_button.h"
#include "app_sensor.h"
#include "app_config.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for main_task */
osThreadId_t main_taskHandle;
const osThreadAttr_t main_task_attributes = {
  .name = "main_task",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for log_task */
osThreadId_t log_taskHandle;
const osThreadAttr_t log_task_attributes = {
  .name = "log_task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for button_scan_tas */
osThreadId_t button_scan_tasHandle;
const osThreadAttr_t button_scan_tas_attributes = {
  .name = "button_scan_tas",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for modbus_master_t */
osThreadId_t modbus_master_tHandle;
const osThreadAttr_t modbus_master_t_attributes = {
  .name = "modbus_master_t",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for flow_refresh_timer */
osTimerId_t flow_refresh_timerHandle;
const osTimerAttr_t flow_refresh_timer_attributes = {
  .name = "flow_refresh_timer"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void main_task_func(void *argument);
void log_task_func(void *argument);
void button_scan_fun(void *argument);
void modbus_master_task_func(void *argument);
void flow_refresh_fun(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* Create the timer(s) */
  /* creation of flow_refresh_timer */
  flow_refresh_timerHandle = osTimerNew(flow_refresh_fun, osTimerPeriodic, NULL, &flow_refresh_timer_attributes);

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  osTimerStart(flow_refresh_timerHandle, 1000);  // 1秒周期
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of main_task */
  main_taskHandle = osThreadNew(main_task_func, NULL, &main_task_attributes);

  /* creation of log_task */
  log_taskHandle = osThreadNew(log_task_func, NULL, &log_task_attributes);

  /* creation of button_scan_tas */
  button_scan_tasHandle = osThreadNew(button_scan_fun, NULL, &button_scan_tas_attributes);

  /* creation of modbus_master_t */
  modbus_master_tHandle = osThreadNew(modbus_master_task_func, NULL, &modbus_master_t_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_main_task_func */
/**
  * @brief  Function implementing the main_task thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_main_task_func */
void main_task_func(void *argument)
{
  /* USER CODE BEGIN main_task_func */
  app_config_init();  // 初始化配置（从EEPROM加载或使用默认值）
  app_sensor_init();  // 初始化传感器模块
  lv_init();  // 初始化LVGL库
  lv_tick_set_cb(xTaskGetTickCount);  // 设置LVGL定时器回调函数，使用FreeRTOS的tick计数
  lv_delay_set_cb(vTaskDelay);  // 设置LVGL延时回调函数，使用FreeRTOS的延时函数
  lv_port_disp_init();  // 初始化显示端口
  // lv_demo_benchmark();  // 初始化演示基准测试
  ui_create();
  /* Infinite loop */
  for(;;)
  {
    uint32_t tick = lv_timer_handler();
    osDelay(pdMS_TO_TICKS(tick));
  }
  /* USER CODE END main_task_func */
}

/* USER CODE BEGIN Header_log_task_func */
/**
* @brief Function implementing the log_task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_log_task_func */
void log_task_func(void *argument)
{
  /* USER CODE BEGIN log_task_func */
  app_log_data_init();
  /* Infinite loop */
  for(;;)
  {
    printf("test_task_func running\r\n");
    osDelay(5000);
  }
  /* USER CODE END log_task_func */
}

/* USER CODE BEGIN Header_button_scan_fun */
/**
* @brief Function implementing the button_scan_tas thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_button_scan_fun */
void button_scan_fun(void *argument)
{
  /* USER CODE BEGIN button_scan_fun */

  /* 初始化应用层按键 */
  app_button_init();

  /* 无限循环 */
  for(;;)
  {
    button_driver_scan(10);  // 10ms扫描周期
    osDelay(10);             // 10ms延时
  }
  /* USER CODE END button_scan_fun */
}

/* USER CODE BEGIN Header_modbus_master_task_func */
/**
* @brief Function implementing the modbus_master_t thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_modbus_master_task_func */
void modbus_master_task_func(void *argument)
{
  /* USER CODE BEGIN modbus_master_task_func */
  /* Infinite loop */
  for(;;)
  {
    app_sensor_poll();
    osDelay(10);  /* 10ms周期 */
  }
  /* USER CODE END modbus_master_task_func */
}

/* flow_refresh_fun function */
void flow_refresh_fun(void *argument)
{
  /* USER CODE BEGIN flow_refresh_fun */
  flow_calc_update();
  /* USER CODE END flow_refresh_fun */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

