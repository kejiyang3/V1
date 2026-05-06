#ifndef __ICM20948_H
#define __ICM20948_H

#include "main.h"

// I2C 地址定义
// ICM20948 7位地址: AD0=GND -> 0x68, AD0=VCC -> 0x69
#define ICM20948_ADDR_7BIT  0x68
#define ICM20948_ADDR       ICM20948_ADDR_7BIT  // 传入 7-bit 地址，由底层 Soft_I2C_* 函数统一左移

// 核心寄存器定义 (Bank 0)
#define REG_BANK_SEL        0x7F
#define REG_WHO_AM_I        0x00
#define REG_USER_CTRL       0x03    // [补上缺失的宏] 用户控制寄存器
#define REG_PWR_MGMT_1      0x06
#define REG_PWR_MGMT_2      0x07
#define REG_INT_PIN_CFG     0x0F    // [补上缺失的宏] 中断/旁路配置寄存器

// 定义传感器数据结构体 (加上了磁力计)
typedef struct {
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    float mag_x;  // 磁力计 X
    float mag_y;  // 磁力计 Y
    float mag_z;  // 磁力计 Z
} IMU_Data_t;

extern IMU_Data_t imu_data;

/* Raw IMU data (for EDF storage) */
extern volatile int16_t raw_ax, raw_ay, raw_az;
extern volatile int16_t raw_gx, raw_gy, raw_gz;
extern volatile int16_t raw_mx, raw_my, raw_mz;

// 函数声明
uint8_t ICM20948_Init(void);
uint8_t ICM20948_Read_Data(void);
uint8_t ICM20948_DataReady(void);
void Soft_I2C_ReadBytes(uint8_t dev_addr, uint8_t reg, uint8_t *buf, uint8_t len);
#endif
