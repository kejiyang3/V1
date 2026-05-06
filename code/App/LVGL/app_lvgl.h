/**
 * @file app_lvgl.h
 * @brief LVGL application layer interface
 * @note This file provides the minimal interface for LVGL integration
 */

#ifndef APP_LVGL_H
#define APP_LVGL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Initialize LVGL core and prepare for display/input drivers
 * @note This function must be called after hardware initialization
 *       and before entering the main loop.
 */
void APP_LVGL_Init(void);

/**
 * @brief Process LVGL tasks (timer handler)
 * @note This function should be called periodically in the main loop.
 *       It handles LVGL internal timers and display refreshes.
 */
void APP_LVGL_Process(void);

/**
 * @brief Create a simple test UI with toggle button
 */
void App_LVGL_TestUI(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LVGL_H */