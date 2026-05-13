#include "max30102.h"
#include "gpio.h"
#include "i2c.h"
#include <stdio.h>
#include "usart.h"
#include "usb_printf.h"

extern I2C_HandleTypeDef hi2c3;

volatile uint8_t max30102_int_flag = 0; // MAX30102 中断标志位（INT 低电平触发)

/** 
  * @brief  检测 MAX30102 是否在线
  * @note   这里保留你的实现方式：用 7-bit 地址左移 1 位探测
  *         若你的 MAX30102_WRITE_ADDR/MAX30102_READ_ADDR 已经是 (addr<<1) 形式，
  *         建议把这里改成 MAX30102_WRITE_ADDR（避免“左移两次”风险）。
  */
ErrorStatus MAX30102_CheckDevice(void)
{
    if (HAL_I2C_IsDeviceReady(&hi2c3, (MAX30102_SLAVE_ADDR << 1), 1, HAL_MAX_DELAY) == HAL_OK)
    {
        usb_printf("MAX30102ConnetSucess\r\n");
        return SUCCESS;
    }
    else
    {
        usb_printf("MAX30102ConnetFiled\r\n");
        return ERROR;
    }
}

/**
  * @brief  写 1 字节寄存器
  */
ErrorStatus MAX30102_WriteByte(uint8_t reg, uint8_t data)
{
    if (HAL_I2C_Mem_Write(&hi2c3, MAX30102_WRITE_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &data, 1, HAL_MAX_DELAY) == HAL_OK)
    {
        return SUCCESS;
    }
    else
    {
        usb_printf("MAX30102writeReg0x%02XFiled\r\n", reg);
        return ERROR;
    }
}

/**
  * @brief  写多个字节
  */
ErrorStatus MAX30102_WriteBuffer(uint8_t reg, uint8_t *buffer, uint16_t len)
{
    if (HAL_I2C_Mem_Write(&hi2c3, MAX30102_WRITE_ADDR, reg, I2C_MEMADD_SIZE_8BIT, buffer, len, HAL_MAX_DELAY) == HAL_OK)
    {
        return SUCCESS;
    }
    else
    {
        usb_printf("MAX30102 MutiByteWriteFiled\r\n");
        return ERROR;
    }
}

/**
  * @brief  读多个字节
  */
ErrorStatus MAX30102_ReadBuffer(uint8_t addr, uint8_t *rbuffer, uint16_t len)
{
    if (HAL_I2C_Mem_Read(&hi2c3, MAX30102_READ_ADDR, addr, I2C_MEMADD_SIZE_8BIT, rbuffer, len, HAL_MAX_DELAY) == HAL_OK)
    {
        return SUCCESS;
    }
    else
    {
        usb_printf("MAX30102 MutiByteReadFiled\r\n");
        return ERROR;
    }
}

/**
  * @brief  初始化 MAX30102（SpO2 模式：RED+IR）
  */
void MAX30102_Init(void)
{
    uint8_t data1, data2;

    while (MAX30102_CheckDevice() == ERROR)
    {
        HAL_Delay(300);
    }

    // Step 0: 设备就绪后立即清除上电默认 PWR_RDY 中断，释放 INT 引脚死锁
    uint8_t clear_status;
    MAX30102_ReadBuffer(INTERRUPT_STATUS1, &clear_status, 1);
    MAX30102_ReadBuffer(INTERRUPT_STATUS2, &clear_status, 1);

    // Step 1: Force software reset (bit6=1)
    while (MAX30102_WriteByte(MODE_CONFIGURATION, 0x40) != SUCCESS);
    HAL_Delay(50);  // Wait for reset to complete (datasheet: reset takes ~1ms)

    // Step 2: Read BOTH interrupt status registers to clear ANY pending interrupts
    // This forces the INT pin HIGH if it was stuck LOW
    while (MAX30102_ReadBuffer(INTERRUPT_STATUS1, &data1, 1) != SUCCESS);
    while (MAX30102_ReadBuffer(INTERRUPT_STATUS2, &data2, 1) != SUCCESS);

    // Step 3: Clear all FIFO pointers and overflow counter
    while (MAX30102_WriteByte(FIFO_WR_POINTER, 0x00) != SUCCESS);
    while (MAX30102_WriteByte(FIFO_OV_COUNTER, 0x00) != SUCCESS);
    while (MAX30102_WriteByte(FIFO_RD_POINTER, 0x00) != SUCCESS);

    // Step 4: Clear FIFO (optional but safe) - read out any stale samples
    uint8_t dummy[6];
    for (int i = 0; i < 32; i++) {
        MAX30102_ReadBuffer(FIFO_DATA, dummy, 6);
    }

    // Step 5: Read status registers AGAIN to ensure INT pin is HIGH
    while (MAX30102_ReadBuffer(INTERRUPT_STATUS1, &data1, 1) != SUCCESS);
    while (MAX30102_ReadBuffer(INTERRUPT_STATUS2, &data2, 1) != SUCCESS);

    // Step 6: Configure FIFO for RTOS Environment
    // SMP_AVE = 010 (4x average)
    // FIFO_ROLLOVER_EN = 1 (Protect against deadlock)
    // FIFO_A_FULL = 1111 (0xF, trigger when 15 empty spaces remain / 17 samples unread)
    // Resulting byte: 0101 1111 = 0x5F
    while (MAX30102_WriteByte(FIFO_CONFIGURATION, 0x5F) != SUCCESS);

    // Step 7: Set mode to SpO2 (RED+IR)
    while (MAX30102_WriteByte(MODE_CONFIGURATION, 0x03) != SUCCESS);

    // Step 8: Configure SpO2 settings
    // 0x2A => ADC_RGE=00(2048nA FS), SR=200sps, LED_PW=215us(17-bit)
    while (MAX30102_WriteByte(SPO2_CONFIGURATION, 0x2A) != SUCCESS);

    // Step 9: Set LED currents (RED=0x2F, IR=0x2F)
    while (MAX30102_WriteByte(LED1_PULSE_AMPLITUDE, 0x2F) != SUCCESS);  // RED LED
    while (MAX30102_WriteByte(LED2_PULSE_AMPLITUDE, 0x2F) != SUCCESS);  // IR LED

    // Step 10: Disable temperature sensor (not needed)
    while (MAX30102_WriteByte(TEMPERATURE_CONFIG, 0x00) != SUCCESS);

    // Step 11 (debug): 禁用所有中断源，仅保留状态读取
    while (MAX30102_WriteByte(INTERRUPT_ENABLE1, 0x00) != SUCCESS);  /* 禁用 A_FULL */
    while (MAX30102_WriteByte(INTERRUPT_ENABLE2, 0x00) != SUCCESS);  /* 禁用其他中断 */

    // Step 12: Final status read to ensure clean start
    while (MAX30102_ReadBuffer(INTERRUPT_STATUS1, &data1, 1) != SUCCESS);
    while (MAX30102_ReadBuffer(INTERRUPT_STATUS2, &data2, 1) != SUCCESS);

    usb_printf("[MAX30102] interrupts disabled and status cleared for debug\r\n");
}

ErrorStatus MAX30102_ClearInterruptStatus(uint8_t *status1, uint8_t *status2)
{
    uint8_t s1 = 0, s2 = 0;
    if (MAX30102_ReadBuffer(INTERRUPT_STATUS1, &s1, 1) != SUCCESS) return ERROR;
    if (MAX30102_ReadBuffer(INTERRUPT_STATUS2, &s2, 1) != SUCCESS) return ERROR;
    if (status1) *status1 = s1;
    if (status2) *status2 = s2;
    return SUCCESS;
}

/**
  * @brief  从 FIFO 读取 1 组样本（SpO2 模式：6 字节 = RED(3B) + IR(3B)）
  * @param  output_data: output_data[0]=IR, output_data[1]=RED（保持你应用层约定）
  * @note   关键点1：SpO2 模式下 FIFO 每个 sample 的顺序是 RED triplet 在前，IR triplet 在后。
  *         关键点2：每个 triplet 24bit 中仅 FIFO_DATA[17:0] 有效，FIFO_DATA[23:18] 不用。
  */
ErrorStatus MAX30102_ReadSample18(uint32_t *ir18, uint32_t *red18)
{
    uint8_t rx[6];

    if (MAX30102_ReadBuffer(FIFO_DATA, rx, 6) != SUCCESS)
        return ERROR;

    /* FIFO 顺序：RED(3B) 在前，IR(3B) 在后 */
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

    if (MAX30102_ReadSample18(&ir18, &red18) != SUCCESS)
    {
        output_data[0] = 0.0f;
        output_data[1] = 0.0f;
        return;
    }

    output_data[0] = (float)ir18;   // IR
    output_data[1] = (float)red18;  // RED
}




/**
  * @brief  获取心率（修复：避免 i+1 越界；避免 temp 未初始化）
  */
uint16_t MAX30102_getHeartRate(float *input_data, uint16_t cache_nums)
{
    if (input_data == NULL || cache_nums < 3) return 0;

    float avg = 0.0f;
    for (uint16_t i = 0; i < cache_nums; i++) avg += input_data[i];
    avg /= cache_nums;

    int16_t first = -1;
    for (uint16_t i = 0; i < (uint16_t)(cache_nums - 1); i++)
    {
        if (input_data[i] > avg && input_data[i + 1] < avg)
        {
            first = (int16_t)i;
            break;
        }
    }
    if (first < 0) return 0;

    int16_t period = -1;
    for (uint16_t i = (uint16_t)(first + 1); i < (uint16_t)(cache_nums - 1); i++)
    {
        if (input_data[i] > avg && input_data[i + 1] < avg)
        {
            period = (int16_t)(i - first);
            break;
        }
    }
    if (period < 0) return 0;

    if (period > 14 && period < 100)
    {
        /* Current configuration: SR=200sps, FIFO average 4 times (SMP_AVE=4), convert to 50Hz keeping original logic */
        return (uint16_t)(3000 / period);
    }
    return 0;
}

/**
  * @brief  获取血氧饱和度（保持你的实现不变）
  */
float MAX30102_getSpO2(float *ir_input_data, float *red_input_data, uint16_t cache_nums)
{
    float ir_max = *ir_input_data, ir_min = *ir_input_data;
    float red_max = *red_input_data, red_min = *red_input_data;
    float R;

    for (uint16_t i = 1; i < cache_nums; i++)
    {
        if (ir_max < ir_input_data[i])  ir_max = ir_input_data[i];
        if (ir_min > ir_input_data[i])  ir_min = ir_input_data[i];
        if (red_max < red_input_data[i]) red_max = red_input_data[i];
        if (red_min > red_input_data[i]) red_min = red_input_data[i];
    }

    R = ((ir_max - ir_min) * red_min) / ((red_max - red_min) * ir_min);
    return ((-45.060f) * R * R + 30.354f * R + 94.845f);
}

/**
  * @brief  轮询读取 MAX30102 FIFO 中的有效数据
  * @param  ir_buf: 存放 IR 数据的数组指针
  * @param  red_buf: 存放 RED 数据的数组指针
  * @param  max_len: 允许读取的最大样本数（防溢出）
  * @retval 实际读取到的样本对数量
  */
uint8_t MAX30102_ReadFIFO_Batch(uint32_t *ir_buf, uint32_t *red_buf, uint8_t max_len) {
    uint8_t status, wr_ptr, rd_ptr, ov_counter;

    /* 读取中断状态寄存器1，判断 Almost Full 标志 (bit 7) */
    if (MAX30102_ReadBuffer(INTERRUPT_STATUS1, &status, 1) != SUCCESS) return 0;
    if ((status & 0x80) == 0) return 0;

    /* 获取 FIFO 指针 */
    MAX30102_ReadBuffer(FIFO_WR_POINTER, &wr_ptr, 1);
    MAX30102_ReadBuffer(FIFO_OV_COUNTER, &ov_counter, 1);
    MAX30102_ReadBuffer(FIFO_RD_POINTER, &rd_ptr, 1);

    /* 计算可用数据量 */
    int8_t num_avail = 0;
    if (wr_ptr == rd_ptr) {
        num_avail = ((ov_counter & 0x0F) != 0) ? 32 : 0;
    } else {
        num_avail = (int8_t)wr_ptr - (int8_t)rd_ptr;
        if (num_avail < 0) num_avail += 32;
    }

    if (num_avail > max_len) num_avail = max_len;

    /* 连续提取样本 */
    for (int8_t i = 0; i < num_avail; i++) {
        MAX30102_ReadSample18(&ir_buf[i], &red_buf[i]);
    }

    /* 满载读取后清除溢出计数器 */
    if (num_avail == 32) {
        MAX30102_WriteByte(FIFO_OV_COUNTER, 0x00);
    }
    return (uint8_t)num_avail;
}

/*********************************************END OF FILE**********************/
