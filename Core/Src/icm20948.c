#include "icm20948.h"
#include "gpio.h"
extern void Safe_USB_Printf(const char *format, ...);

IMU_Data_t imu_data = {0};

/* Raw IMU data (for EDF storage) */
volatile int16_t raw_ax = 0, raw_ay = 0, raw_az = 0;
volatile int16_t raw_gx = 0, raw_gy = 0, raw_gz = 0;
volatile int16_t raw_mx = 0, raw_my = 0, raw_mz = 0;

extern I2C_HandleTypeDef hi2c3;

// 2. 软件 I2C 读写寄存器封装
// =====================================================================
static void Soft_I2C_WriteReg(uint8_t dev_addr, uint8_t reg, uint8_t data) {
    HAL_I2C_Mem_Write(&hi2c3, (dev_addr << 1), reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

static uint8_t Soft_I2C_ReadReg(uint8_t dev_addr, uint8_t reg) {
    uint8_t val = 0;
    HAL_I2C_Mem_Read(&hi2c3, (dev_addr << 1), reg, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
    return val;
}

void Soft_I2C_ReadBytes(uint8_t dev_addr, uint8_t reg, uint8_t *buf, uint8_t len) {
    HAL_I2C_Mem_Read(&hi2c3, (dev_addr << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}

static void ICM_SelectBank(uint8_t bank) {
    Soft_I2C_WriteReg(ICM20948_ADDR, REG_BANK_SEL, bank << 4);
}

// =====================================================================
// 3. ICM-20948 初始化与读取 (集成中断清零与9轴)
// =====================================================================
uint8_t ICM20948_Init(void) {
    HAL_Delay(10);

    /* CRITICAL: Verify I2C address is 8-bit (with R/W bit) not 7-bit
       ICM20948_ADDR must be: 0xD0 (0x68 << 1), NOT 0x68
       If communication fails with WHO_AM_I, check icm20948.h:
         #define ICM20948_ADDR (ICM20948_ADDR_7BIT << 1)  [CORRECT]
       NOT:
         #define ICM20948_ADDR 0x68  [WRONG - missing left shift]
    */

    ICM_SelectBank(0);
    uint8_t who_am_i = Soft_I2C_ReadReg(ICM20948_ADDR, REG_WHO_AM_I);
    if (who_am_i != 0xEA) {
        /* WHO_AM_I mismatch → likely I2C address error or chip not connected */
        return 1;
    }

    // 1. 唤醒并复位
    Soft_I2C_WriteReg(ICM20948_ADDR, REG_PWR_MGMT_1, 0x80); 
    HAL_Delay(50);
    Soft_I2C_WriteReg(ICM20948_ADDR, REG_PWR_MGMT_1, 0x01); 
    Soft_I2C_WriteReg(ICM20948_ADDR, REG_PWR_MGMT_2, 0x00); 

    // 2. 配置量程与采样率: Bank 2
    ICM_SelectBank(2);

    /* 2a. 量程配置 */
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x14, 0x02); // ACCEL_CONFIG: +-4g
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x01, 0x03); // GYRO_CONFIG_1: +-500dps

    /* 2b. 采样率分频配置 (目标接近 50Hz) */
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x09, 0x01); // ODR_ALIGN_EN: 使能 ODR 对齐

    /* Accel ODR = 1125Hz / (1 + div)
       目标 50Hz: div = 21 (0x15) -> 1125/22 = 51.14 Hz
    */
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x10, 0x00); // ACCEL_SMPLRT_DIV_1
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x11, 0x15); // ACCEL_SMPLRT_DIV_2

    /* Gyro ODR = 1125Hz / (1 + div)
       目标 50Hz: div = 21 (0x15) -> 1125/22 = 51.14 Hz
    */
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x00, 0x15); // GYRO_SMPLRT_DIV = 21

    // 3. 开启内部 AK09916 磁力计
    ICM_SelectBank(0);
    Soft_I2C_WriteReg(ICM20948_ADDR, REG_INT_PIN_CFG, 0xC2); // Bypass使能 + ActiveLow + OpenDrain
    HAL_Delay(10);
    
    uint8_t ak_addr = 0x0C;
    Soft_I2C_WriteReg(ak_addr, 0x32, 0x01); // 磁力计软复位
    HAL_Delay(10);
    Soft_I2C_WriteReg(ak_addr, 0x31, 0x08); // 100Hz 连续模式4
    
    Soft_I2C_WriteReg(ICM20948_ADDR, REG_INT_PIN_CFG, 0xC0); // 关闭Bypass，保持ActiveLow+OpenDrain
    
    ICM_SelectBank(3);
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x01, 0x07);        
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x03, 0x0C | 0x80); 
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x04, 0x11);        
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x05, 0x80 | 0x08); 
    
    ICM_SelectBank(0);
    Soft_I2C_WriteReg(ICM20948_ADDR, REG_USER_CTRL, 0x20); // 使能 Master

    // ==========================================================
    // 【调试模式】：禁用全部 ICM 中断源 + 清全部 INT_STATUS
    // INT_PIN_CFG = 0xC0: INT1_ACTL(bit7)=1 低电平有效, INT1_OPEN(bit6)=1 开漏输出
    // ==========================================================
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x0F, 0xC0);   // INT_PIN_CFG: Active Low, Open-Drain
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x10, 0x00);   // INT_ENABLE: 禁用
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x11, 0x00);   // INT_ENABLE_1: 禁用 Data Ready
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x12, 0x00);   // INT_ENABLE_2: 禁用
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x13, 0x00);   // INT_ENABLE_3: 禁用

    // 读取所有 INT_STATUS 以清除 pending 中断
    uint8_t dummy;
    dummy = Soft_I2C_ReadReg(ICM20948_ADDR, 0x19); (void)dummy;  // INT_STATUS
    dummy = Soft_I2C_ReadReg(ICM20948_ADDR, 0x1A); (void)dummy;  // INT_STATUS_1
    dummy = Soft_I2C_ReadReg(ICM20948_ADDR, 0x1B); (void)dummy;  // INT_STATUS_2
    dummy = Soft_I2C_ReadReg(ICM20948_ADDR, 0x1C); (void)dummy;  // INT_STATUS_3

    Safe_USB_Printf("[ICM20948] interrupts disabled and status cleared for debug\r\n");

    return 0;
}

/**
  * @brief  查询 ICM20948 数据是否就绪（轮询模式）
  * @return 1 = 数据就绪，0 = 数据未就绪
  */
uint8_t ICM20948_DataReady(void) {
    ICM_SelectBank(0);
    // INT_STATUS_1 (0x1A) bit 0 = RAW_DATA_RDY_INT
    return (Soft_I2C_ReadReg(ICM20948_ADDR, 0x1A) & 0x01);
}

uint8_t ICM20948_Read_Data(void) {
    uint8_t buf[22] = {0};
    int16_t local_ax, local_ay, local_az, local_gx, local_gy, local_gz, local_mx, local_my, local_mz;

    ICM_SelectBank(0);

    if (HAL_I2C_Mem_Read(&hi2c3, (ICM20948_ADDR << 1), 0x2D, I2C_MEMADD_SIZE_8BIT, buf, 22, 10) != HAL_OK) {
        return 1;
    }

    local_ax = (int16_t)((buf[0] << 8) | buf[1]);
    local_ay = (int16_t)((buf[2] << 8) | buf[3]);
    local_az = (int16_t)((buf[4] << 8) | buf[5]);

    local_gx = (int16_t)((buf[6] << 8)  | buf[7]);
    local_gy = (int16_t)((buf[8] << 8)  | buf[9]);
    local_gz = (int16_t)((buf[10] << 8) | buf[11]);

    local_mx = (int16_t)((buf[15] << 8) | buf[14]);
    local_my = (int16_t)((buf[17] << 8) | buf[16]);
    local_mz = (int16_t)((buf[19] << 8) | buf[18]);

    raw_ax = local_ax; raw_ay = local_ay; raw_az = local_az;
    raw_gx = local_gx; raw_gy = local_gy; raw_gz = local_gz;
    raw_mx = local_mx; raw_my = local_my; raw_mz = local_mz;

    imu_data.accel_x = (float)local_ax / 8192.0f;
    imu_data.accel_y = (float)local_ay / 8192.0f;
    imu_data.accel_z = (float)local_az / 8192.0f;

    imu_data.gyro_x = (float)local_gx / 65.5f;
    imu_data.gyro_y = (float)local_gy / 65.5f;
    imu_data.gyro_z = (float)local_gz / 65.5f;

    imu_data.mag_x = (float)local_mx * 0.15f;
    imu_data.mag_y = (float)local_my * 0.15f;
    imu_data.mag_z = (float)local_mz * 0.15f;

    return 0;
}