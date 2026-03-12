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
/* Definitions for test_task */
osThreadId_t test_taskHandle;
const osThreadAttr_t test_task_attributes = {
  .name = "test_task",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void main_task_func(void *argument);
void test_task_func(void *argument);

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

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of main_task */
  main_taskHandle = osThreadNew(main_task_func, NULL, &main_task_attributes);

  /* creation of test_task */
  test_taskHandle = osThreadNew(test_task_func, NULL, &test_task_attributes);

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
  lv_init();  // 初始化LVGL库
  lv_tick_set_cb(xTaskGetTickCount);  // 设置LVGL定时器回调函数，使用FreeRTOS的tick计数
  lv_delay_set_cb(vTaskDelay);  // 设置LVGL延时回调函数，使用FreeRTOS的延时函数

  lv_port_disp_init();  // 初始化显示端口
  //lv_demo_widgets();  // 初始化演示小部件
  lv_demo_benchmark();  // 初始化演示基准测试
  /* Infinite loop */
  for(;;)
  {
    uint32_t tick = lv_timer_handler();
    osDelay(pdMS_TO_TICKS(tick));
  }
  /* USER CODE END main_task_func */
}

/* USER CODE BEGIN Header_test_task_func */
/**
* @brief Function implementing the test_task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_test_task_func */
void test_task_func(void *argument)
{
  /* USER CODE BEGIN test_task_func */
  // fsmc_st7789_init();
  // fsmc_st7789_test_pattern();

  /* Infinite loop */
  for(;;)
  {
    printf("test_task_func running\r\n");
    osDelay(1000);
  }
  /* USER CODE END test_task_func */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

