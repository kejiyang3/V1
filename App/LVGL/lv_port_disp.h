/**
 * @file lv_port_disp.h
 * @brief LVGL display driver porting interface
 * @note This is the first-stage bring-up implementation for STM32L496 + LCD_1IN69.
 *       Uses single partial buffer and blocking transfer (LCD_1IN69_DisplayWindows).
 */

#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Initialize LVGL display driver
 *
 * This function:
 * 1. Initializes draw buffer (single partial buffer)
 * 2. Configures display driver with flush callback
 * 3. Registers the driver with LVGL
 *
 * @note Must be called after lv_init() and before creating any UI objects.
 */
void lv_port_disp_init(void);

/**
 * @brief Notify LVGL that DMA transfer is complete
 *
 * This function should be called from HAL_SPI_TxCpltCallback
 * after raising the CS pin.
 */
void lv_port_disp_dma_complete(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_DISP_H */