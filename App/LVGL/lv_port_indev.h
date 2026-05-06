/**
 * @file lv_port_indev.h
 * @brief LVGL input device driver porting for CST816 touch screen
 * @note This is the touch driver implementation for STM32L496 + CST816.
 *       Uses I2C communication with interrupt-based touch detection.
 */

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Initialize LVGL input device driver
 *
 * This function:
 * 1. Initializes CST816 touch screen hardware
 * 2. Configures input driver with touchpad read callback
 * 3. Registers the driver with LVGL
 *
 * @note Must be called after lv_init() and before creating any UI objects.
 */
void lv_port_indev_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_INDEV_H */