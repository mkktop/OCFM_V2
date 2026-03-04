/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LCD_RS_Pin GPIO_PIN_6
#define LCD_RS_GPIO_Port GPIOE
#define UART4_TX_Pin GPIO_PIN_0
#define UART4_TX_GPIO_Port GPIOA
#define UART4_RX_Pin GPIO_PIN_1
#define UART4_RX_GPIO_Port GPIOA
#define UART2_TX_Pin GPIO_PIN_2
#define UART2_TX_GPIO_Port GPIOA
#define UART2_RX_Pin GPIO_PIN_3
#define UART2_RX_GPIO_Port GPIOA
#define RELAY1_CTRL_Pin GPIO_PIN_4
#define RELAY1_CTRL_GPIO_Port GPIOA
#define RELAY2_CTRL_Pin GPIO_PIN_5
#define RELAY2_CTRL_GPIO_Port GPIOA
#define RELAY3_CTRL_Pin GPIO_PIN_6
#define RELAY3_CTRL_GPIO_Port GPIOA
#define RELAY4_CTRL_Pin GPIO_PIN_7
#define RELAY4_CTRL_GPIO_Port GPIOA
#define CT1820_Pin GPIO_PIN_0
#define CT1820_GPIO_Port GPIOB
#define PWM01_Pin GPIO_PIN_1
#define PWM01_GPIO_Port GPIOB
#define LCD_D4_Pin GPIO_PIN_7
#define LCD_D4_GPIO_Port GPIOE
#define LCD_D5_Pin GPIO_PIN_8
#define LCD_D5_GPIO_Port GPIOE
#define LCD_D6_Pin GPIO_PIN_9
#define LCD_D6_GPIO_Port GPIOE
#define LCD_D7_Pin GPIO_PIN_10
#define LCD_D7_GPIO_Port GPIOE
#define LCD_RESET_Pin GPIO_PIN_14
#define LCD_RESET_GPIO_Port GPIOE
#define EEPROM_SCL_Pin GPIO_PIN_10
#define EEPROM_SCL_GPIO_Port GPIOB
#define EEPROM_SDA_Pin GPIO_PIN_11
#define EEPROM_SDA_GPIO_Port GPIOB
#define LORA_NSS_Pin GPIO_PIN_12
#define LORA_NSS_GPIO_Port GPIOB
#define LORA_SCK_Pin GPIO_PIN_13
#define LORA_SCK_GPIO_Port GPIOB
#define LORA_MISO_Pin GPIO_PIN_14
#define LORA_MISO_GPIO_Port GPIOB
#define LORA_MOSI_Pin GPIO_PIN_15
#define LORA_MOSI_GPIO_Port GPIOB
#define ML307_TX_Pin GPIO_PIN_8
#define ML307_TX_GPIO_Port GPIOD
#define ML307_RX_Pin GPIO_PIN_9
#define ML307_RX_GPIO_Port GPIOD
#define LORA_DIO1_Pin GPIO_PIN_10
#define LORA_DIO1_GPIO_Port GPIOD
#define LOAR_DIO0_Pin GPIO_PIN_11
#define LOAR_DIO0_GPIO_Port GPIOD
#define LORA_RESET_Pin GPIO_PIN_12
#define LORA_RESET_GPIO_Port GPIOD
#define LCD_BLK_Pin GPIO_PIN_13
#define LCD_BLK_GPIO_Port GPIOD
#define LCD_D0_Pin GPIO_PIN_14
#define LCD_D0_GPIO_Port GPIOD
#define LCD_D1_Pin GPIO_PIN_15
#define LCD_D1_GPIO_Port GPIOD
#define KEY_4_Pin GPIO_PIN_6
#define KEY_4_GPIO_Port GPIOC
#define ML307_EN_Pin GPIO_PIN_7
#define ML307_EN_GPIO_Port GPIOC
#define KEY_3_Pin GPIO_PIN_8
#define KEY_3_GPIO_Port GPIOA
#define UART1_TX_Pin GPIO_PIN_9
#define UART1_TX_GPIO_Port GPIOA
#define UART1_RX_Pin GPIO_PIN_10
#define UART1_RX_GPIO_Port GPIOA
#define UART1_CTRL_Pin GPIO_PIN_11
#define UART1_CTRL_GPIO_Port GPIOA
#define KEY_2_Pin GPIO_PIN_12
#define KEY_2_GPIO_Port GPIOA
#define KEY_1_Pin GPIO_PIN_15
#define KEY_1_GPIO_Port GPIOA
#define LCD_D2_Pin GPIO_PIN_0
#define LCD_D2_GPIO_Port GPIOD
#define LCD_D3_Pin GPIO_PIN_1
#define LCD_D3_GPIO_Port GPIOD
#define SDIO_WP_Pin GPIO_PIN_3
#define SDIO_WP_GPIO_Port GPIOD
#define LCD_RD_Pin GPIO_PIN_4
#define LCD_RD_GPIO_Port GPIOD
#define LCD_WR_Pin GPIO_PIN_5
#define LCD_WR_GPIO_Port GPIOD
#define UART2_CTRL_Pin GPIO_PIN_6
#define UART2_CTRL_GPIO_Port GPIOD
#define LCD_CS_Pin GPIO_PIN_7
#define LCD_CS_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
