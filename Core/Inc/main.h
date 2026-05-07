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
#include "stm32l4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
#define BLE_RX_BUF_SIZE 256
/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* External variables from main.c for audio/ECG recording control */
extern volatile uint8_t request_stop_audio;
extern volatile uint8_t request_stop_ecg;
extern volatile uint8_t is_usb_streaming;

/* ECG streaming control */
extern volatile uint8_t ecg_streaming;

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ECG_CS_Pin GPIO_PIN_13
#define ECG_CS_GPIO_Port GPIOC
#define ICM_INT_Pin GPIO_PIN_1
#define ICM_INT_GPIO_Port GPIOH
#define PPG_INT_Pin GPIO_PIN_2
#define PPG_INT_GPIO_Port GPIOC
#define KEY_BTN_Pin GPIO_PIN_1
#define KEY_BTN_GPIO_Port GPIOA
#define LCD_RST_Pin GPIO_PIN_2
#define LCD_RST_GPIO_Port GPIOA
#define INT_TOUCH_Pin GPIO_PIN_3
#define INT_TOUCH_GPIO_Port GPIOA
#define INT_TOUCH_EXTI_IRQn EXTI3_IRQn
#define RST_TOUCH_Pin GPIO_PIN_4
#define RST_TOUCH_GPIO_Port GPIOA
#define LCD_CS_Pin GPIO_PIN_4
#define LCD_CS_GPIO_Port GPIOC
#define LCD_DC_Pin GPIO_PIN_5
#define LCD_DC_GPIO_Port GPIOC
#define LCD_BLK_Pin GPIO_PIN_1
#define LCD_BLK_GPIO_Port GPIOB
#define EN_MIC_Pin GPIO_PIN_12
#define EN_MIC_GPIO_Port GPIOB
#define LINK_Pin GPIO_PIN_6
#define LINK_GPIO_Port GPIOC
#define RST_BLUETOOTH_Pin GPIO_PIN_7
#define RST_BLUETOOTH_GPIO_Port GPIOC
#define ECG_INT_Pin GPIO_PIN_6
#define ECG_INT_GPIO_Port GPIOB
#define ECG_INT_EXTI_IRQn EXTI9_5_IRQn
#define SD_DETECT_Pin GPIO_PIN_7
#define SD_DETECT_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* ===== MAX30003 心电中断读取配置 ===== */
#define ECG_SAMPLE_RATE_HZ          512       /* ECG采样率 */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
