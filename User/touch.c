/******************************************************************************
 * @file    touch.c
 * @brief   CST816 Touch Screen Driver (HAL Library)
 * @note    Using I2C2 (hi2c2) with address 0x2A
 ******************************************************************************/
#include "touch.h"
#include "main.h"

/* External I2C handle (defined in main.c) */
extern I2C_HandleTypeDef hi2c2;
extern void MX_I2C2_Init(void);  /* 用于 I2C 恢复重初始化 */

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
static void CST816_ResetPin(void)
{
    HAL_GPIO_WritePin(RST_TOUCH_GPIO_Port, RST_TOUCH_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(RST_TOUCH_GPIO_Port, RST_TOUCH_Pin, GPIO_PIN_SET);
    HAL_Delay(100);
}

static uint8_t CST816_VerifyChip(void)
{
    uint8_t chip_id = 0;
    /* 连续读 3 次，排除 I2C 偶发失败 */
    for (int i = 0; i < 3; i++) {
        if (CST816_Read_Reg(CST816_REG_CHIP_ID, &chip_id, 1)) {
            return 1;
        }
        HAL_Delay(10);
    }
    return 0;
}

void CST816_Init(void)
{
    /* 最多重试 3 次，每次失败后硬件复位 + I2C 恢复 */
    for (int retry = 0; retry < 3; retry++) {
        if (retry > 0) {
            /* 重试时做 I2C 总线复位 */
            HAL_I2C_DeInit(&hi2c2);
            HAL_Delay(10);
            MX_I2C2_Init();
            HAL_Delay(20);
        }

        CST816_ResetPin();

        CST816_Write_Reg(CST816_REG_DIS_AUTO_SLEEP, 0x01);
        CST816_Write_Reg(CST816_REG_IRQ_CTRL, 0x60);

        if (CST816_VerifyChip()) {
            /* 清除可能存在的待处理触摸数据 */
            uint8_t dummy[6];
            CST816_Read_Reg(CST816_REG_GESTURE_ID, dummy, 6);

            g_touch.gesture = 0;
            g_touch.finger_num = 0;
            g_touch.x = 0;
            g_touch.y = 0;
            g_touch.touch_event = 0;
            return; /* 初始化成功 */
        }
    }
    /* 3 次重试均失败 — 触摸不工作，但系统继续运行 */
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
    static uint8_t s_i2c_err_count = 0;
    uint8_t data[6];
    HAL_StatusTypeDef res = HAL_I2C_Mem_Read(&hi2c2, CST816_I2C_ADDR, CST816_REG_GESTURE_ID,
                                              I2C_MEMADD_SIZE_8BIT, data, 6, 10);

    if (res != HAL_OK) {
        s_i2c_err_count++;
        /* 连续 3 次 I2C 失败 → 复位 I2C 外设 + 重新初始化 CST816 */
        if (s_i2c_err_count >= 3) {
            s_i2c_err_count = 0;
            HAL_I2C_DeInit(&hi2c2);
            HAL_Delay(10);
            MX_I2C2_Init();
            HAL_Delay(20);
            CST816_ResetPin();
            CST816_Write_Reg(CST816_REG_DIS_AUTO_SLEEP, 0x01);
            CST816_Write_Reg(CST816_REG_IRQ_CTRL, 0x60);
            uint8_t dummy[6];
            CST816_Read_Reg(CST816_REG_GESTURE_ID, dummy, 6);
        }
        return 0;
    }

    s_i2c_err_count = 0;  /* 成功，清零错误计数 */

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

