#include "max30102.h"
#include "gpio.h"
#include "i2c.h"
#include "app_log.h"
#include <stdio.h>
#include "usart.h"

extern I2C_HandleTypeDef hi2c3;

volatile uint8_t max30102_int_flag = 0;

/* 写 I2C 寄存器 helper */
static ErrorStatus _write_reg(uint8_t reg, uint8_t data)
{
    if (HAL_I2C_Mem_Write(&hi2c3, MAX30102_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 10) == HAL_OK)
        return SUCCESS;
    return ERROR;
}

/* 读 I2C 寄存器 helper */
static ErrorStatus _read_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (HAL_I2C_Mem_Read(&hi2c3, MAX30102_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 10) == HAL_OK)
        return SUCCESS;
    return ERROR;
}

ErrorStatus MAX30102_CheckDevice(void)
{
    if (HAL_I2C_IsDeviceReady(&hi2c3, MAX30102_I2C_ADDR, 1, 10) == HAL_OK) {
        return SUCCESS;
    }
    return ERROR;
}

ErrorStatus MAX30102_WriteByte(uint8_t reg, uint8_t data)
{
    return _write_reg(reg, data);
}

ErrorStatus MAX30102_WriteBuffer(uint8_t reg, uint8_t *buffer, uint16_t len)
{
    if (HAL_I2C_Mem_Write(&hi2c3, MAX30102_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, buffer, len, 10) == HAL_OK)
        return SUCCESS;
    return ERROR;
}

ErrorStatus MAX30102_ReadBuffer(uint8_t addr, uint8_t *rbuffer, uint16_t len)
{
    return _read_regs(addr, rbuffer, len);
}

/**
  * @brief  初始化 MAX30102 — 有限重试，不会无限卡死
  * @retval MAX30102_INIT_OK / _NOT_FOUND / _CONFIG_FAILED
  */
MAX30102_InitResult_t MAX30102_Init(void)
{
    uint8_t data1, data2;

    /* Step 1: 检测设备在线 (最多 5 次) */
    uint8_t found = 0;
    for (int i = 0; i < 5; i++) {
        if (MAX30102_CheckDevice() == SUCCESS) {
            found = 1;
            break;
        }
        HAL_Delay(50);
    }
    if (!found) {
        return MAX30102_INIT_NOT_FOUND;
    }

    /* Step 2: 清上电中断 */
    _read_regs(INTERRUPT_STATUS1, &data1, 1);
    _read_regs(INTERRUPT_STATUS2, &data2, 1);

    /* Step 3: 软复位 */
    for (int i = 0; i < 5; i++) {
        if (_write_reg(MODE_CONFIGURATION, 0x40) == SUCCESS) break;
        if (i == 4) return MAX30102_INIT_CONFIG_FAILED;
        HAL_Delay(10);
    }
    HAL_Delay(50);

    /* 清中断 */
    _read_regs(INTERRUPT_STATUS1, &data1, 1);
    _read_regs(INTERRUPT_STATUS2, &data2, 1);

    /* Step 4: 清 FIFO 指针 */
    for (int i = 0; i < 5; i++) {
        if (_write_reg(FIFO_WR_POINTER, 0x00) == SUCCESS) break;
        if (i == 4) return MAX30102_INIT_CONFIG_FAILED;
    }
    for (int i = 0; i < 5; i++) {
        if (_write_reg(FIFO_OV_COUNTER, 0x00) == SUCCESS) break;
        if (i == 4) return MAX30102_INIT_CONFIG_FAILED;
    }
    for (int i = 0; i < 5; i++) {
        if (_write_reg(FIFO_RD_POINTER, 0x00) == SUCCESS) break;
        if (i == 4) return MAX30102_INIT_CONFIG_FAILED;
    }

    /* Step 5: 排出旧 FIFO 数据 */
    uint8_t dummy[6];
    for (int i = 0; i < 32; i++) {
        _read_regs(FIFO_DATA, dummy, 6);
    }

    /* 再次清中断 */
    _read_regs(INTERRUPT_STATUS1, &data1, 1);
    _read_regs(INTERRUPT_STATUS2, &data2, 1);

    /* Step 6: 配置寄存器 (有限重试) */
    if (_write_reg(FIFO_CONFIGURATION, 0x5F) != SUCCESS) return MAX30102_INIT_CONFIG_FAILED;
    if (_write_reg(MODE_CONFIGURATION, 0x03) != SUCCESS) return MAX30102_INIT_CONFIG_FAILED;
    if (_write_reg(SPO2_CONFIGURATION, 0x2A) != SUCCESS) return MAX30102_INIT_CONFIG_FAILED;
    if (_write_reg(LED1_PULSE_AMPLITUDE, 0x2F) != SUCCESS) return MAX30102_INIT_CONFIG_FAILED;
    if (_write_reg(LED2_PULSE_AMPLITUDE, 0x2F) != SUCCESS) return MAX30102_INIT_CONFIG_FAILED;
    if (_write_reg(TEMPERATURE_CONFIG, 0x00) != SUCCESS) return MAX30102_INIT_CONFIG_FAILED;
    if (_write_reg(INTERRUPT_ENABLE1, 0x00) != SUCCESS) return MAX30102_INIT_CONFIG_FAILED;
    if (_write_reg(INTERRUPT_ENABLE2, 0x00) != SUCCESS) return MAX30102_INIT_CONFIG_FAILED;

        /* 最终清中断 */
    _read_regs(INTERRUPT_STATUS1, &data1, 1);
    _read_regs(INTERRUPT_STATUS2, &data2, 1);

    /* 清除上电 PWR_RDY 中断，释放 INT 引脚 */
    {
        uint8_t pwr_rdy;
        _read_regs(INTERRUPT_STATUS1, &pwr_rdy, 1);
        APP_USB_LOG("[MAX30102] Init done. Cleared initial INT status: 0x%02X\r\n", pwr_rdy);
    }

    /* === 诊断：强制使能中断 + 寄存器 Dump + FIFO 验证 === */

    /* 强制使能 A_FULL + PPG_RDY 中断，覆盖 0x00 */
    _write_reg(INTERRUPT_ENABLE1, 0xC0);  /* bit7=A_FULL, bit6 = PPG_RDY */

    {
        uint8_t reg_dump[6];

        _read_regs(INTERRUPT_ENABLE1,     &reg_dump[0], 1);
        _read_regs(INTERRUPT_ENABLE2,     &reg_dump[1], 1);
        _read_regs(FIFO_CONFIGURATION,    &reg_dump[2], 1);
        _read_regs(MODE_CONFIGURATION,    &reg_dump[3], 1);
        _read_regs(SPO2_CONFIGURATION,    &reg_dump[4], 1);
        _read_regs(LED1_PULSE_AMPLITUDE,  &reg_dump[5], 1);

        APP_USB_LOG("[DIAG] MAX30102 REG DUMP:\r\n");
        APP_USB_LOG("  - INT_EN1 (0x02) = 0x%02X (Expect 0xC0: A_FULL|PPG_RDY)\r\n", reg_dump[0]);
        APP_USB_LOG("  - INT_EN2 (0x03) = 0x%02X\r\n", reg_dump[1]);
        APP_USB_LOG("  - FIFO_CFG(0x08) = 0x%02X\r\n", reg_dump[2]);
        APP_USB_LOG("  - MODE_CFG(0x09) = 0x%02X (Expect 0x03 for SpO2)\r\n", reg_dump[3]);
        APP_USB_LOG("  - SPO2_CFG(0x0A) = 0x%02X\r\n", reg_dump[4]);
        APP_USB_LOG("  - LED1_PA (0x0C) = 0x%02X (Must not be 0x00)\r\n", reg_dump[5]);
    }

    /* 验证 FIFO 写指针是否在递增（相隔 50ms） */
    {
        uint8_t wr_ptr1 = 0, wr_ptr2;
        _read_regs(FIFO_WR_POINTER, &wr_ptr1, 1);
        wr_ptr1 &= 0x1F;
        HAL_Delay(50);
        _read_regs(FIFO_WR_POINTER, &wr_ptr2, 1);
        wr_ptr2 &= 0x1F;

        APP_USB_LOG("[DIAG] FIFO Write Pointer: T1=%d, T2=%d\r\n", wr_ptr1, wr_ptr2);

        if (wr_ptr1 == wr_ptr2) {
            APP_USB_LOG("[DIAG] FATAL: FIFO is NOT updating! Sensor is NOT sampling. Check MODE and LED PA.\r\n");
        } else {
            APP_USB_LOG("[DIAG] FIFO IS UPDATING. Sensor is running. The issue is STRICTLY the INT pin routing/config.\r\n");
        }
    }

    return MAX30102_INIT_OK;
}

ErrorStatus MAX30102_ClearInterruptStatus(uint8_t *status1, uint8_t *status2)
{
    uint8_t s1 = 0, s2 = 0;
    if (_read_regs(INTERRUPT_STATUS1, &s1, 1) != SUCCESS) return ERROR;
    if (_read_regs(INTERRUPT_STATUS2, &s2, 1) != SUCCESS) return ERROR;
    if (status1) *status1 = s1;
    if (status2) *status2 = s2;
    return SUCCESS;
}

ErrorStatus MAX30102_ReadSample18(uint32_t *ir18, uint32_t *red18)
{
    uint8_t rx[6];

    if (_read_regs(FIFO_DATA, rx, 6) != SUCCESS)
        return ERROR;

    uint32_t red = ((((uint32_t)rx[0]) << 16) |
                    (((uint32_t)rx[1]) << 8)  |
                     ((uint32_t)rx[2])) & 0x03FFFFu;

    uint32_t ir  = ((((uint32_t)rx[3]) << 16) |
                    (((uint32_t)rx[4]) << 8)  |
                     ((uint32_t)rx[5])) & 0x03FFFFu;

    *ir18  = ir;
    *red18 = red;
    return SUCCESS;
}

void MAX30102_fifo_read(float *output_data)
{
    uint32_t ir18, red18;

    if (MAX30102_ReadSample18(&ir18, &red18) != SUCCESS) {
        output_data[0] = 0.0f;
        output_data[1] = 0.0f;
        return;
    }

    output_data[0] = (float)ir18;
    output_data[1] = (float)red18;
}

uint16_t MAX30102_getHeartRate(float *input_data, uint16_t cache_nums)
{
    if (input_data == NULL || cache_nums < 3) return 0;

    float avg = 0.0f;
    for (uint16_t i = 0; i < cache_nums; i++) avg += input_data[i];
    avg /= cache_nums;

    int16_t first = -1;
    for (uint16_t i = 0; i < (uint16_t)(cache_nums - 1); i++) {
        if (input_data[i] > avg && input_data[i + 1] < avg) {
            first = (int16_t)i;
            break;
        }
    }
    if (first < 0) return 0;

    int16_t period = -1;
    for (uint16_t i = (uint16_t)(first + 1); i < (uint16_t)(cache_nums - 1); i++) {
        if (input_data[i] > avg && input_data[i + 1] < avg) {
            period = (int16_t)(i - first);
            break;
        }
    }
    if (period < 0) return 0;

    if (period > 14 && period < 100) {
        return (uint16_t)(3000 / period);
    }
    return 0;
}

float MAX30102_getSpO2(float *ir_input_data, float *red_input_data, uint16_t cache_nums)
{
    float ir_max = *ir_input_data, ir_min = *ir_input_data;
    float red_max = *red_input_data, red_min = *red_input_data;
    float R;

    for (uint16_t i = 1; i < cache_nums; i++) {
        if (ir_max < ir_input_data[i])  ir_max = ir_input_data[i];
        if (ir_min > ir_input_data[i])  ir_min = ir_input_data[i];
        if (red_max < red_input_data[i]) red_max = red_input_data[i];
        if (red_min > red_input_data[i]) red_min = red_input_data[i];
    }

    R = ((ir_max - ir_min) * red_min) / ((red_max - red_min) * ir_min);
    return ((-45.060f) * R * R + 30.354f * R + 94.845f);
}

uint8_t MAX30102_ReadFIFO_Batch(uint32_t *ir_buf, uint32_t *red_buf, uint8_t max_len)
{
    uint8_t status, wr_ptr, rd_ptr, ov_counter;

    /* 读取 INTERRUPT_STATUS1 以释放 MAX30102 INT 引脚 */
    if (_read_regs(INTERRUPT_STATUS1, &status, 1) != SUCCESS) {
        status = 0;
    }
    /* NOTE: 不再因 A_FULL bit 未置位就直接 return 0。
     * 即使 status & 0x80 == 0，仍继续根据 FIFO 指针判断是否有数据。
     * 原因：A_FULL 可能因溢出/时序原因不置位，但 FIFO 中仍有样本。 */

    _read_regs(FIFO_WR_POINTER, &wr_ptr, 1);
    _read_regs(FIFO_OV_COUNTER, &ov_counter, 1);
    _read_regs(FIFO_RD_POINTER, &rd_ptr, 1);

    int8_t num_avail = 0;
    if (wr_ptr == rd_ptr) {
        num_avail = ((ov_counter & 0x0F) != 0) ? 32 : 0;
    } else {
        num_avail = (int8_t)wr_ptr - (int8_t)rd_ptr;
        if (num_avail < 0) num_avail += 32;
    }

    if (num_avail > max_len) num_avail = max_len;

    for (int8_t i = 0; i < num_avail; i++) {
        MAX30102_ReadSample18(&ir_buf[i], &red_buf[i]);
    }

    if (num_avail == 32) {
        _write_reg(FIFO_OV_COUNTER, 0x00);
    }
    return (uint8_t)num_avail;
}

ErrorStatus MAX30102_EnableFifoAlmostFullInterrupt(void)
{
    /* 清 pending */
    uint8_t s1, s2;
    MAX30102_ClearInterruptStatus(&s1, &s2);

    /* 使能 A_FULL 中断 */
    if (_write_reg(INTERRUPT_ENABLE1, 0x80) != SUCCESS) return ERROR;
    if (_write_reg(INTERRUPT_ENABLE2, 0x00) != SUCCESS) return ERROR;

    /* 再次清 pending */
    MAX30102_ClearInterruptStatus(&s1, &s2);

    return SUCCESS;
}

ErrorStatus MAX30102_DisableInterrupts(void)
{
    uint8_t s1, s2;
    if (_write_reg(INTERRUPT_ENABLE1, 0x00) != SUCCESS) return ERROR;
    if (_write_reg(INTERRUPT_ENABLE2, 0x00) != SUCCESS) return ERROR;
    MAX30102_ClearInterruptStatus(&s1, &s2);
    return SUCCESS;
}

/**
  * @brief  调试：纯软件轮询 PPG_INT 引脚电平 (绕过 EXTI)
  * @note   连续 200 次 × 10ms = 2 秒采样，确认 TXS0104E 转换后的信号状态。
  *         结果通过 USB CDC 输出。
  */
void MAX30102_Debug_Poll_INT_Pin(void)
{
    uint32_t high_cnt = 0, low_cnt = 0;

    APP_USB_LOG("[DIAG] Start Polling MAX30102 PPG_INT Pin (PH1) for 2 seconds...\r\n");

    for (int i = 0; i < 200; i++) {
        if (HAL_GPIO_ReadPin(PPG_INT_GPIO_Port, PPG_INT_Pin) == GPIO_PIN_SET) {
            high_cnt++;
        } else {
            low_cnt++;
        }
        HAL_Delay(10);
    }

    APP_USB_LOG("[DIAG] PPG_INT Polling Result: High=%lu, Low=%lu\r\n", high_cnt, low_cnt);

    if (low_cnt == 0) {
        APP_USB_LOG("[DIAG] PPG_INT is ALWAYS HIGH! TXS0104E disconnected, or MAX30102 not asserting INT.\r\n");
    } else if (high_cnt == 0) {
        APP_USB_LOG("[DIAG] PPG_INT is ALWAYS LOW! Interrupt not cleared (check PWR_RDY), or TXS0104E latch-up.\r\n");
    }
}
