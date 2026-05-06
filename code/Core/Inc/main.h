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
#define ICM_INT_EXTI_IRQn EXTI1_IRQn
#define PPG_INT_Pin GPIO_PIN_2
#define PPG_INT_GPIO_Port GPIOC
#define PPG_INT_EXTI_IRQn EXTI2_IRQn
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
#define ECG_SAMPLE_RATE_HZ          512       /* ECG采样率 (由CNFG_ECG配置决定) */
#define ECG_FIFO_TRIGGER_SAMPLES    10        /* 每次中断触发读取的样本数 (匹配MNGR_INT EFIT) */
#define ECG_DOUBLE_BUF_SAMPLES      256       /* 双缓冲每个缓冲区样本数 (≈0.5s@512Hz) */

/* MAX30003 EN_INT 寄存器: 启用硬件中断，INTB输出为推挽高电平有效
 * Bit 23(EN_EINT)=1, Bit 1:0(INTB_TYPE)=10(active high)
 * 配合STM32 EXTI上升沿触发 (GPIO_MODE_IT_RISING) */
#define ECG_INT_EN_VALUE            0x800002UL

/** USB CDC 发送包格式 (每包) */
#define ECG_USB_TAG                 0xEC      /* 同步头标签 */
#define ECG_USB_TYPE_ECG            0x01      /* 数据类型: ECG波形 */
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
