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
/* 7-bit 地址 0x57，HAL 接口需左移 1 位 */
#define MAX30102_SLAVE_ADDR     0x57
#define MAX30102_I2C_ADDR       (MAX30102_SLAVE_ADDR << 1)   /* 0xAE, 给 HAL 使用 */

/* ========== MAX30102 寄存器地址 ========== */
#define INTERRUPT_STATUS1    0x00
#define INTERRUPT_STATUS2    0x01
#define INTERRUPT_ENABLE1    0x02
#define INTERRUPT_ENABLE2    0x03
#define FIFO_WR_POINTER      0x04
#define FIFO_OV_COUNTER      0x05
#define FIFO_RD_POINTER      0x06
#define FIFO_DATA            0x07
#define FIFO_CONFIGURATION   0x08
#define MODE_CONFIGURATION   0x09
#define SPO2_CONFIGURATION   0x0A
#define LED1_PULSE_AMPLITUDE 0x0C
#define LED2_PULSE_AMPLITUDE 0x0D
#define MULTI_LED_MODE_CTRL1 0x11
#define MULTI_LED_MODE_CTRL2 0x12
#define TEMPERATURE_INTEGER  0x1F
#define TEMPERATURE_FRACTION 0x20
#define TEMPERATURE_CONFIG   0x21
#define REVISION_ID          0xFE
#define PART_ID              0xFF

/* ========== 初始化结果 ========== */
typedef enum {
    MAX30102_INIT_OK = 0,
    MAX30102_INIT_NOT_FOUND,
    MAX30102_INIT_CONFIG_FAILED
} MAX30102_InitResult_t;

/* ========== 对外 API ========== */
void MAX30102_INT_Init(void);
ErrorStatus MAX30102_CheckDevice(void);
ErrorStatus MAX30102_ClearInterruptStatus(uint8_t *status1, uint8_t *status2);
ErrorStatus MAX30102_WriteByte(uint8_t reg, uint8_t data);
ErrorStatus MAX30102_WriteBuffer(uint8_t reg, uint8_t *buffer, uint16_t len);
ErrorStatus MAX30102_ReadBuffer(uint8_t addr, uint8_t *rbuffer, uint16_t len);
MAX30102_InitResult_t MAX30102_Init(void);
ErrorStatus MAX30102_ReadSample18(uint32_t *ir18, uint32_t *red18);
void MAX30102_fifo_read(float *output_data);
uint8_t MAX30102_ReadFIFO_Batch(uint32_t *ir_buf, uint32_t *red_buf, uint8_t max_len);
uint16_t MAX30102_getHeartRate(float *input_data, uint16_t cache_nums);
float MAX30102_getSpO2(float *ir_input_data, float *red_input_data, uint16_t cache_nums);

/* 中断使能/禁用 (recording start/stop 时调用) */
ErrorStatus MAX30102_EnableFifoAlmostFullInterrupt(void);
ErrorStatus MAX30102_DisableInterrupts(void);

/* 调试：纯软件轮询 PPG_INT 引脚电平 (绕过 EXTI) */
void MAX30102_Debug_Poll_INT_Pin(void);

extern volatile uint8_t max30102_int_flag;

#ifdef __cplusplus
}
#endif

#endif /* __MAX30102_H__ */
