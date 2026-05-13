/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.h
  * @brief   This file contains all the function prototypes for
  *          the i2c.c file
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
#ifndef __I2C_H__
#define __I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;

/* USER CODE BEGIN Private defines */

/* ========== I2C3 诊断模式选择 ========== */
#define I2C3_DIAG_MODE_NORMAL             0   /* 正常系统运行 */
#define I2C3_DIAG_MODE_OD_SCL_SDA_10HZ    1   /* SCL/SDA 开漏 10/5Hz, 测 TXS 翻译 */
#define I2C3_DIAG_MODE_PP_ALL4_10HZ       2   /* 4 线推挽 10Hz, 测通道通断 */
#define I2C3_DIAG_MODE_HW_I2C_PROBE_LOOP  3   /* 真实 I2C 探测, 测 START/ACK */

#ifndef I2C3_DIAG_MODE
#define I2C3_DIAG_MODE I2C3_DIAG_MODE_HW_I2C_PROBE_LOOP
#endif

/* USER CODE END Private defines */

void MX_I2C2_Init(void);
void MX_I2C3_Init(void);

/* USER CODE BEGIN Prototypes */

void I2C3_PP_All4_Test10Hz(void);
void I2C3_OD_SclSda_Test10Hz(void);
void I2C3_HW_ProbeLoop(void);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __I2C_H__ */
