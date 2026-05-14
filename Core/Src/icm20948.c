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
/* 原始版本 (兼容现有调用方) */
static void Soft_I2C_WriteReg(uint8_t dev_addr, uint8_t reg, uint8_t data) {
    HAL_I2C_Mem_Write(&hi2c3, (dev_addr << 1), reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

static uint8_t Soft_I2C_ReadReg(uint8_t dev_addr, uint8_t reg) {
    uint8_t val = 0;
    HAL_I2C_Mem_Read(&hi2c3, (dev_addr << 1), reg, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
    return val;
}

/* checked 版本 — 返回 HAL 状态，不再将 I2C 失败伪装为 0 */
static HAL_StatusTypeDef Soft_I2C_WriteReg_Checked(uint8_t dev_addr, uint8_t reg, uint8_t data) {
    return HAL_I2C_Mem_Write(&hi2c3, (dev_addr << 1), reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

static HAL_StatusTypeDef Soft_I2C_ReadReg_Checked(uint8_t dev_addr, uint8_t reg, uint8_t *val) {
    if (val == NULL) return HAL_ERROR;
    *val = 0;
    return HAL_I2C_Mem_Read(&hi2c3, (dev_addr << 1), reg, I2C_MEMADD_SIZE_8BIT, val, 1, 100);
}

void Soft_I2C_ReadBytes(uint8_t dev_addr, uint8_t reg, uint8_t *buf, uint8_t len) {
    HAL_I2C_Mem_Read(&hi2c3, (dev_addr << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}

static void ICM_SelectBank(uint8_t bank) {
    Soft_I2C_WriteReg(ICM20948_ADDR, REG_BANK_SEL, bank << 4);
}

static HAL_StatusTypeDef ICM_SelectBank_Checked(uint8_t bank) {
    return Soft_I2C_WriteReg_Checked(ICM20948_ADDR, REG_BANK_SEL, bank << 4);
}

// =====================================================================
// 3. ICM-20948 Init (checked write + post-init verify + 10x WHO_AM_I)
// =====================================================================
uint8_t ICM20948_Init(void)
{
    HAL_StatusTypeDef s;
    uint8_t val;

    HAL_Delay(10);

    /*
     * Address convention in this project:
     * - ICM20948_ADDR is stored as 7-bit address 0x68.
     * - Soft_I2C_ReadReg_Checked / Soft_I2C_WriteReg_Checked shift
     *   it left by 1 when passing to STM32 HAL.
     */

    /* --- 0. 初始 WHO_AM_I --- */
    s = Soft_I2C_ReadReg_Checked(ICM20948_ADDR, REG_WHO_AM_I, &val);
    if (s != HAL_OK || val != 0xEA) return 1;

    /* --- 1. 唤醒并复位 (return code 2) --- */
    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, REG_PWR_MGMT_1, 0x80) != HAL_OK) return 2;
    HAL_Delay(50);
    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, REG_PWR_MGMT_1, 0x01) != HAL_OK) return 2;
    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, REG_PWR_MGMT_2, 0x00) != HAL_OK) return 2;

    /* --- 2. Bank 2: 量程与采样率 (return code 3) --- */
    if (ICM_SelectBank_Checked(2) != HAL_OK) return 3;

    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x14, 0x02) != HAL_OK) return 3; // ACCEL_CONFIG: +-4g
    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x01, 0x03) != HAL_OK) return 3; // GYRO_CONFIG_1: +-500dps

    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x09, 0x01) != HAL_OK) return 3; // ODR_ALIGN_EN

    /* Accel ODR = 1125Hz / (1 + div): div=21 -> 51.14Hz */
    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x10, 0x00) != HAL_OK) return 3;
    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x11, 0x15) != HAL_OK) return 3;

    /* Gyro ODR = 1125Hz / (1 + div): div=21 -> 51.14Hz */
    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x00, 0x15) != HAL_OK) return 3;

    /* --- 3. 磁力计 AK09916 + I2C Master (编译开关) --- */
#if ICM20948_ENABLE_MAG_MASTER
    ICM_SelectBank_Checked(0);
    Soft_I2C_WriteReg_Checked(ICM20948_ADDR, REG_INT_PIN_CFG, 0xC2); // Bypass enable
    HAL_Delay(10);

    {
        uint8_t ak_addr = 0x0C;
        Soft_I2C_WriteReg_Checked(ak_addr, 0x32, 0x01); // mag soft reset
        HAL_Delay(10);
        Soft_I2C_WriteReg_Checked(ak_addr, 0x31, 0x08); // 100Hz continuous mode4
    }

    Soft_I2C_WriteReg_Checked(ICM20948_ADDR, REG_INT_PIN_CFG, 0xC0); // close bypass

    ICM_SelectBank_Checked(3);
    Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x01, 0x07);
    Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x03, 0x0C | 0x80);
    Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x04, 0x11);
    Soft_I2C_WriteReg_Checked(ICM20948_ADDR, 0x05, 0x80 | 0x08);

    ICM_SelectBank_Checked(0);
    Soft_I2C_WriteReg_Checked(ICM20948_ADDR, REG_USER_CTRL, 0x20); // Master enable
#endif

    /* --- 4. 返回 Bank 0, 中断引脚配置 (return code 4) --- */
    if (ICM_SelectBank_Checked(0) != HAL_OK) return 4;
    if (Soft_I2C_WriteReg_Checked(ICM20948_ADDR, REG_INT_PIN_CFG, 0xC0) != HAL_OK) return 4;

    /* 清全部 INT_STATUS */
    {
        uint8_t dummy;
        Soft_I2C_ReadReg_Checked(ICM20948_ADDR, 0x19, &dummy);
        Soft_I2C_ReadReg_Checked(ICM20948_ADDR, 0x1A, &dummy);
        Soft_I2C_ReadReg_Checked(ICM20948_ADDR, 0x1B, &dummy);
        Soft_I2C_ReadReg_Checked(ICM20948_ADDR, 0x1C, &dummy);
    }

    /* --- 5. 初始化后立即回读关键寄存器 (return code 5=读失败, 6=值不匹配) --- */
    {
        uint8_t whoami_post, pwr1_post, pwr2_post, pin_cfg_post;

        if (Soft_I2C_ReadReg_Checked(ICM20948_ADDR, REG_WHO_AM_I,     &whoami_post)   != HAL_OK) return 5;
        if (Soft_I2C_ReadReg_Checked(ICM20948_ADDR, REG_PWR_MGMT_1,   &pwr1_post)     != HAL_OK) return 5;
        if (Soft_I2C_ReadReg_Checked(ICM20948_ADDR, REG_PWR_MGMT_2,   &pwr2_post)     != HAL_OK) return 5;
        if (Soft_I2C_ReadReg_Checked(ICM20948_ADDR, REG_INT_PIN_CFG,  &pin_cfg_post)  != HAL_OK) return 5;

        if (whoami_post  != 0xEA) return 6;
        if (pwr1_post    != 0x01) return 6;
        if (pwr2_post    != 0x00) return 6;
        if (pin_cfg_post != 0xC0) return 6;
    }

    /* --- 6. 10 次连续 WHO_AM_I 读回, 间隔 20ms (return code 7) --- */
    for (int i = 0; i < 10; i++) {
        HAL_Delay(20);
        s = Soft_I2C_ReadReg_Checked(ICM20948_ADDR, REG_WHO_AM_I, &val);
        if (s != HAL_OK || val != 0xEA) return 7;
    }

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

/* ---- 中断清除 / Data Ready 使能 / 原始数据读取 ---- */

void ICM20948_ClearInterruptStatus(void)
{
    uint8_t tmp;
    ICM_SelectBank(0);
    tmp = Soft_I2C_ReadReg(ICM20948_ADDR, 0x19); (void)tmp;
    tmp = Soft_I2C_ReadReg(ICM20948_ADDR, 0x1A); (void)tmp;
    tmp = Soft_I2C_ReadReg(ICM20948_ADDR, 0x1B); (void)tmp;
    tmp = Soft_I2C_ReadReg(ICM20948_ADDR, 0x1C); (void)tmp;
}

void ICM20948_EnableDataReadyInterrupt(void)
{
    ICM_SelectBank(0);
    /* INT_PIN_CFG: Active Low, Open-Drain (与 I2C 电平转换匹配) */
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x0F, 0xC0);
    ICM20948_ClearInterruptStatus();
    /* INT_ENABLE_1 bit0 = RAW_DATA_0_RDY_EN */
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x11, 0x01);
}

void ICM20948_DisableDataReadyInterrupt(void)
{
    ICM_SelectBank(0);
    /* INT_ENABLE_1: 禁用 Data Ready */
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x11, 0x00);
    ICM20948_ClearInterruptStatus();
}

uint8_t ICM20948_ReadBank0Reg_Debug(uint8_t reg)
{
    ICM_SelectBank(0);
    return Soft_I2C_ReadReg(ICM20948_ADDR, reg);
}

HAL_StatusTypeDef ICM20948_ReadBank0Reg_Checked(uint8_t reg, uint8_t *val)
{
    if (ICM_SelectBank_Checked(0) != HAL_OK) return HAL_ERROR;
    return Soft_I2C_ReadReg_Checked(ICM20948_ADDR, reg, val);
}

void ICM20948_EnableLatchedDataReadyInterrupt_Debug(void)
{
    ICM_SelectBank(0);

    /*
     * INT_PIN_CFG = 0xE0
     * bit7 INT1_ACTL = 1: active low
     * bit6 INT1_OPEN = 1: open drain
     * bit5 INT1_LATCH_EN = 1: latch until status cleared
     */
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x0F, 0xE0);

    ICM20948_ClearInterruptStatus();

    /*
     * INT_ENABLE_1 bit0 = RAW_DATA_0_RDY_EN
     */
    Soft_I2C_WriteReg(ICM20948_ADDR, 0x11, 0x01);
}

uint8_t ICM20948_ReadAccelGyroRaw(int16_t *ax, int16_t *ay, int16_t *az,
                                  int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t buf[12];
    if (!ax || !ay || !az || !gx || !gy || !gz) return 1;

    ICM_SelectBank(0);
    if (HAL_I2C_Mem_Read(&hi2c3, (ICM20948_ADDR << 1), 0x2D, I2C_MEMADD_SIZE_8BIT, buf, 12, 10) != HAL_OK) {
        return 1;
    }

    *ax = (int16_t)((buf[0] << 8) | buf[1]);
    *ay = (int16_t)((buf[2] << 8) | buf[3]);
    *az = (int16_t)((buf[4] << 8) | buf[5]);
    *gx = (int16_t)((buf[6] << 8) | buf[7]);
    *gy = (int16_t)((buf[8] << 8) | buf[9]);
    *gz = (int16_t)((buf[10] << 8) | buf[11]);

    raw_ax = *ax; raw_ay = *ay; raw_az = *az;
    raw_gx = *gx; raw_gy = *gy; raw_gz = *gz;

    ICM20948_ClearInterruptStatus();
    return 0;
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