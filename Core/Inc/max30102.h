#ifndef __MAX30102_H__
#define __MAX30102_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* ========== 硬件引脚定义 (请根据实际硬件连接修改) ========== */
#ifndef MAX30102_Pin
#define MAX30102_Pin         GPIO_PIN_0   /* 默认 PA0，实际请修改 */
#endif
#ifndef MAX30102_GPIO_Port
#define MAX30102_GPIO_Port   GPIOA
#endif

#ifndef SPO2_NS_Pin
#define SPO2_NS_Pin          GPIO_PIN_1   /* 默认 PA1，实际请修改 */
#endif
#ifndef SPO2_NS_GPIO_Port
#define SPO2_NS_GPIO_Port    GPIOA
#endif

/* ========== MAX30102 I2C 地址 ========== */
#define MAX30102_SLAVE_ADDR     0x57      /* 7-bit I2C address */
#define MAX30102_WRITE_ADDR     (MAX30102_SLAVE_ADDR << 1)
#define MAX30102_READ_ADDR      ((MAX30102_SLAVE_ADDR << 1) | 0x01)

/* ========== MAX30102 寄存器地址 ========== */
/* 状态寄存器 */
#define INTERRUPT_STATUS1    0x00
#define INTERRUPT_STATUS2    0x01
#define INTERRUPT_ENABLE1    0x02
#define INTERRUPT_ENABLE2    0x03

/* FIFO 寄存器 */
#define FIFO_WR_POINTER      0x04
#define FIFO_OV_COUNTER      0x05
#define FIFO_RD_POINTER      0x06
#define FIFO_DATA            0x07

/* 配置寄存器 */
#define FIFO_CONFIGURATION   0x08
#define MODE_CONFIGURATION   0x09
#define SPO2_CONFIGURATION   0x0A
#define LED1_PULSE_AMPLITUDE 0x0C
#define LED2_PULSE_AMPLITUDE 0x0D
#define MULTI_LED_MODE_CTRL1 0x11
#define MULTI_LED_MODE_CTRL2 0x12

/* 温度寄存器 */
#define TEMPERATURE_INTEGER  0x1F
#define TEMPERATURE_FRACTION 0x20
#define TEMPERATURE_CONFIG   0x21

/* 器件信息 */
#define REVISION_ID          0xFE
#define PART_ID              0xFF

/* ========== 对外 API ========== */
void MAX30102_INT_Init(void);
ErrorStatus MAX30102_CheckDevice(void);
ErrorStatus MAX30102_WriteByte(uint8_t reg, uint8_t data);
ErrorStatus MAX30102_WriteBuffer(uint8_t reg, uint8_t *buffer, uint16_t len);
ErrorStatus MAX30102_ReadBuffer(uint8_t addr, uint8_t *rbuffer, uint16_t len);
void MAX30102_Init(void);
ErrorStatus MAX30102_ReadSample18(uint32_t *ir18, uint32_t *red18);
void MAX30102_fifo_read(float *output_data);
uint8_t MAX30102_ReadFIFO_Batch(uint32_t *ir_buf, uint32_t *red_buf, uint8_t max_len);
uint16_t MAX30102_getHeartRate(float *input_data, uint16_t cache_nums);
float MAX30102_getSpO2(float *ir_input_data, float *red_input_data, uint16_t cache_nums);

extern volatile uint8_t max30102_int_flag;

#ifdef __cplusplus
}
#endif

#endif /* __MAX30102_H__ */