/*****************************************************************************
* | File        :   usb_printf.c
* | Function    :   USB CDC printf wrapper implementation
* | Info        :   Redirect printf output to USB CDC (Virtual COM Port)
*
******************************************************************************/
#include "usb_printf.h"
#include "stm32l4xx_hal.h"

#define USB_PRINTF_BUFFER_SIZE 512
#define USB_TRANSMIT_TIMEOUT 2000    // ms - 等待USB传输完成的超时时间（增加到2秒）
#define USB_MAX_RETRY_COUNT 10       // 最大重试次数

/**
 * USB CDC formatted print function
 * Redirect printf-style output to USB CDC with retry mechanism
 */
int usb_printf(const char *format, ...)
{
    va_list args;
    int len;
    uint32_t start_tick;
    uint8_t result;
    static uint8_t usb_print_buffer[USB_PRINTF_BUFFER_SIZE];
    int retry_count = 0;

    // Format the string
    va_start(args, format);
    len = vsnprintf((char *)usb_print_buffer, USB_PRINTF_BUFFER_SIZE, format, args);
    va_end(args);

    // Limit length to buffer size
    if (len < 0) {
        len = 0;
    } else if (len >= USB_PRINTF_BUFFER_SIZE) {
        len = USB_PRINTF_BUFFER_SIZE - 1;
        usb_print_buffer[len] = '\0'; // Ensure null termination
    }

    // Send via USB CDC with timeout and retry
    if (len > 0) {
        start_tick = HAL_GetTick();
        do {
            result = CDC_Transmit_FS(usb_print_buffer, len);
            if (result == USBD_OK) {
                break;  // 成功发送
            } else if (result == USBD_BUSY) {
                // 等待一小段时间后重试
                HAL_Delay(10);
                retry_count++;
            } else {
                // 其他错误，等待更长时间后重试
                HAL_Delay(50);
                retry_count++;
            }
        } while ((HAL_GetTick() - start_tick) < USB_TRANSMIT_TIMEOUT && retry_count < USB_MAX_RETRY_COUNT);
    }

    return len;
}

/**
 * Direct USB CDC transmit with timeout
 */
void usb_transmit(uint8_t *buf, uint16_t len)
{
    uint32_t start_tick;
    uint8_t result;
    int retry_count = 0;

    if (buf != NULL && len > 0) {
        start_tick = HAL_GetTick();
        do {
            result = CDC_Transmit_FS(buf, len);
            if (result == USBD_OK) {
                break;  // 成功发送
            } else if (result == USBD_BUSY) {
                HAL_Delay(10);
                retry_count++;
            } else {
                HAL_Delay(50);
                retry_count++;
            }
        } while ((HAL_GetTick() - start_tick) < USB_TRANSMIT_TIMEOUT && retry_count < USB_MAX_RETRY_COUNT);
    }
}