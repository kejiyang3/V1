/*****************************************************************************
* | File        :   usb_printf.h
* | Function    :   USB CDC printf wrapper
* | Info        :   Redirect printf output to USB CDC (Virtual COM Port)
*
******************************************************************************/
#ifndef __USB_PRINTF_H
#define __USB_PRINTF_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "usbd_cdc_if.h"

/**
 * USB CDC formatted print function
 * Usage: usb_printf("Hello %d\r\n", 123);
 */
int usb_printf(const char *format, ...);

/**
 * Direct USB CDC transmit function
 * Usage: usb_transmit((uint8_t*)"Hello", 5);
 */
void usb_transmit(uint8_t *buf, uint16_t len);

#endif /* __USB_PRINTF_H */