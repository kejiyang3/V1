/******************************************************************************
 * @file    touch.h
 * @brief   CST816 Touch Screen Driver (HAL Library)
 * @note    I2C Address: 0x15 << 1 = 0x2A
 ******************************************************************************/
#ifndef __TOUCH_H
#define __TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "i2c.h"

/* I2C Device Address */
#define CST816_I2C_ADDR    (0x15 << 1)  /* 0x2A */

/* CST816 Register Definitions */
#define CST816_REG_GESTURE_ID      0x01  /* Gesture ID */
#define CST816_REG_FINGER_NUM      0x02  /* Finger Number */
#define CST816_REG_XPOS_H          0x03  /* X Position High 4 bits */
#define CST816_REG_XPOS_L          0x04  /* X Position Low 8 bits */
#define CST816_REG_YPOS_H          0x05  /* Y Position High 4 bits */
#define CST816_REG_YPOS_L          0x06  /* Y Position Low 8 bits */
#define CST816_REG_CHIP_ID         0xA7  /* Chip ID */
#define CST816_REG_MOTION_MASK     0xEC  /* Motion Mask */
#define CST816_REG_AUTO_SLEEP_TIME 0xF9  /* Auto Sleep Time */
#define CST816_REG_IRQ_CTRL        0xFA  /* Interrupt Control */
#define CST816_REG_AUTO_RESET      0xFB  /* Auto Reset (No Gesture Sleep) */
#define CST816_REG_LONG_PRESS_TIME 0xFC  /* Long Press Time */
#define CST816_REG_DIS_AUTO_SLEEP  0xFE  /* Disable Auto Sleep (Enable Low Power) */

/* Touch Data Structure */
typedef struct {
    uint8_t gesture;      /* Gesture ID */
    uint8_t finger_num;   /* Number of fingers */
    uint16_t x;           /* X coordinate (0-239) */
    uint16_t y;           /* Y coordinate (0-279) */
    uint8_t touch_event;  /* Touch event: 0 = no touch, 1 = touch down, 2 = touch up, 3 = touch contact */
} TouchPoint_t;

/* Global touch data */
extern TouchPoint_t g_touch;

/* Function Prototypes */
void CST816_Init(void);
uint8_t CST816_Write_Reg(uint8_t reg, uint8_t value);
uint8_t CST816_Read_Reg(uint8_t reg, uint8_t *buf, uint16_t len);
uint8_t CST816_Get_XY(void);
uint8_t CST816_GetAction(uint16_t *X, uint16_t *Y, uint8_t *Gesture);

#ifdef __cplusplus
}
#endif

#endif /* __TOUCH_H */