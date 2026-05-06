/******************************************************************************
 * @file    touch.c
 * @brief   CST816 Touch Screen Driver (HAL Library)
 * @note    Using I2C2 (hi2c2) with address 0x2A
 ******************************************************************************/
#include "touch.h"
#include "main.h"

/* External I2C handle (defined in main.c) */
extern I2C_HandleTypeDef hi2c2;

/* Global touch data structure */
TouchPoint_t g_touch;

/******************************************************************************
 * @brief   Write a value to CST816 register
 * @param   reg     Register address
 * @param   value   Value to write
 * @return  HAL status (HAL_OK on success)
 ******************************************************************************/
uint8_t CST816_Write_Reg(uint8_t reg, uint8_t value)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Write(&hi2c2, CST816_I2C_ADDR, reg,
                               I2C_MEMADD_SIZE_8BIT, &value, 1, 10);

    return (status == HAL_OK) ? 1 : 0;
}

/******************************************************************************
 * @brief   Read values from CST816 registers
 * @param   reg     Starting register address
 * @param   buf     Buffer to store read data
 * @param   len     Number of bytes to read
 * @return  HAL status (HAL_OK on success)
 ******************************************************************************/
uint8_t CST816_Read_Reg(uint8_t reg, uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(&hi2c2, CST816_I2C_ADDR, reg,
                              I2C_MEMADD_SIZE_8BIT, buf, len, 10);

    return (status == HAL_OK) ? 1 : 0;
}

/******************************************************************************
 * @brief   Initialize CST816 touch controller
 * @note    Performs hardware reset and basic configuration
 ******************************************************************************/
void CST816_Init(void)
{
    /* 1. 极其关键的硬件复位唤醒时序（严格遵守手册时间） */
    HAL_GPIO_WritePin(RST_TOUCH_GPIO_Port, RST_TOUCH_Pin, GPIO_PIN_RESET);
    HAL_Delay(10); // 拉低至少 10ms
    HAL_GPIO_WritePin(RST_TOUCH_GPIO_Port, RST_TOUCH_Pin, GPIO_PIN_SET);
    HAL_Delay(50); // 拉高后等待至少 50ms，让IC完成启动

    /* 2. 停止自动休眠 (寄存器 0xFE 写 0x01) */
    CST816_Write_Reg(CST816_REG_DIS_AUTO_SLEEP, 0x01);

    /* 3. 配置中断模式 (寄存器 0xFA 写 0x60 表示连续报点) */
    /* Bit6=EnTouch, Bit5=EnChange, Bit4=EnMotion */
    CST816_Write_Reg(CST816_REG_IRQ_CTRL, 0x60); // 只要按住，中断引脚就会每隔10ms周期性发出低脉冲

    /* 4. (可选) 配置报点率 (寄存器 0xEE) - 使用默认值 10ms */
    /* CST816_Write_Reg(0xEE, 0x01); */ // 默认其实也是1 (10ms)

    /* 5. 验证是否通信成功 (读取 Chip ID, 寄存器 0xA7) */
    uint8_t chip_id = 0;
    if (CST816_Read_Reg(CST816_REG_CHIP_ID, &chip_id, 1)) {
        /* 芯片ID读取成功，预期值为 0xB4 或 0xB5 */
        /* 可在此添加调试输出：usb_printf("[DEBUG] CST816 Chip ID: 0x%02X\\r\\n", chip_id); */
    }

    /* 6. 清除可能存在的待处理触摸数据 */
    uint8_t dummy[6];
    CST816_Read_Reg(CST816_REG_GESTURE_ID, dummy, 6);

    /* 7. 初始化全局触摸结构体 */
    g_touch.gesture = 0;
    g_touch.finger_num = 0;
    g_touch.x = 0;
    g_touch.y = 0;
    g_touch.touch_event = 0;
}

/******************************************************************************
 * @brief   Read touch coordinates and update global structure
 * @note    Reads 5 registers starting from Finger_Num (0x02)
 * @return  1 if touch data is read successfully, 0 if I2C fails
 ******************************************************************************/
uint8_t CST816_Get_XY(void)
{
    uint8_t data[5];
    static uint8_t prev_finger_num = 0; /* 跟踪上一次的手指状态，用于检测 Toucn Down/Up 切换 */

    /* 1. 从 0x02 (Finger Num) 开始连续读取 5 个字节 */
    if (!CST816_Read_Reg(CST816_REG_FINGER_NUM, data, 5)) {
        return 0; /* I2C 读取失败 */
    }

    /* 2. 解析手指数量 (第0个字节) */
    g_touch.finger_num = data[0] & 0x0F;

    /* 3. 状态机：检测触摸事件（Down / Up / Contact / None） */
    if (g_touch.finger_num == 0) {
        /* 没有手指按下 */
        g_touch.x = 0;
        g_touch.y = 0;

        if (prev_finger_num > 0) {
            g_touch.touch_event = 2; /* Touch Up: 手指刚刚抬起 */
        } else {
            g_touch.touch_event = 0; /* None: 持续无触摸 */
        }
        prev_finger_num = 0;
        return 1;
    }

    /* 4. 解析 X/Y 坐标
       data[1] = 0x03 (X_H), data[2] = 0x04 (X_L)
       data[3] = 0x05 (Y_H), data[4] = 0x06 (Y_L) */
    g_touch.x = ((data[1] & 0x0F) << 8) | data[2];
    g_touch.y = ((data[3] & 0x0F) << 8) | data[4];

    /* 验证坐标有效性 (屏幕边界: 240x280) */
    if (g_touch.x >= 240 || g_touch.y >= 280) {
        g_touch.finger_num = 0; /* 无效坐标，当作无触摸 */
        g_touch.x = 0;
        g_touch.y = 0;
        if (prev_finger_num > 0) {
            g_touch.touch_event = 2; /* Touch Up: 从有效触摸变为无效 */
        } else {
            g_touch.touch_event = 0;
        }
        prev_finger_num = 0;
        return 1;
    }

    /* 5. 有效触摸 - 判断是 Down 还是 Contact */
    if (prev_finger_num == 0) {
        g_touch.touch_event = 1; /* Touch Down: 刚按下 */
    } else {
        g_touch.touch_event = 3; /* Touch Contact: 持续按住 */
    }
    prev_finger_num = g_touch.finger_num;

    return 1; /* 读取成功 */
}

/******************************************************************************
 * @brief   Read touch data in one I2C transaction (gesture + finger + X + Y)
 * @param   X         Pointer to store X coordinate
 * @param   Y         Pointer to store Y coordinate
 * @param   Gesture   Pointer to store gesture ID
 * @return  Finger count (0 = no touch), or 0 on I2C failure
 ******************************************************************************/
uint8_t CST816_GetAction(uint16_t *X, uint16_t *Y, uint8_t *Gesture)
{
    uint8_t data[6];
    HAL_StatusTypeDef res = HAL_I2C_Mem_Read(&hi2c2, CST816_I2C_ADDR, CST816_REG_GESTURE_ID,
                                              I2C_MEMADD_SIZE_8BIT, data, 6, 10);

    if (res != HAL_OK) {
        return 0; /* I2C 读取失败 */
    }

    *Gesture = data[0];
    uint8_t finger = data[1] & 0x0F;
    *X = (uint16_t)((data[2] & 0x0F) << 8) | data[3];
    *Y = (uint16_t)((data[4] & 0x0F) << 8) | data[5];

    /* 过滤无效超大坐标 (屏幕 240x280) */
    if (*X > 300 || *Y > 300) {
        return 0;
    }

    return finger;
}

