/**
 * @file lv_port_indev.c
 * @brief LVGL input device driver porting implementation for CST816 touch screen
 * @note This is the touch driver implementation for STM32L496 + CST816.
 *       Uses I2C communication with interrupt-based touch detection.
 */

#include "lv_port_indev.h"
#include "lvgl.h"
#include <stdbool.h>
#include "../../User/touch.h" // 引入我们自己的底层触摸驱动
#include "../../User/Config/usb_printf.h"  // 确保引入你的串口/USB打印头文件

/* External variable for wake-up touch locking */
volatile uint8_t block_touch_flag = 0;

/* 触摸手势 — 供 app_lvgl.c 读取做页面切换 */
volatile uint8_t g_touch_gesture = 0;

/*-------------------------------------------
 *  Static functions
 *------------------------------------------*/

/**
 * @brief Touchpad read callback for LVGL input device driver
 *
 * This function is called periodically by LVGL to read touch state.
 * It reads the touch coordinates from CST816 via I2C and reports
 * the state (pressed/released) and coordinates to LVGL.
 *
 * @note When finger is released, LVGL still needs the last valid coordinates.
 *
 * @param indev_drv Pointer to LVGL input device driver
 * @param data      Pointer to touch data structure (output)
 */
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    static bool prev_pressed = false;

    uint16_t x, y;
    uint8_t gesture;
    uint8_t finger = CST816_GetAction(&x, &y, &gesture);

    bool is_pressed = (finger > 0);

    if(is_pressed) {
        if (block_touch_flag) {
            data->state = LV_INDEV_STATE_REL;
            data->point.x = last_x;
            data->point.y = last_y;
            return;
        }

        last_x = x;
        last_y = y;
        data->state = LV_INDEV_STATE_PR;

        if (!prev_pressed) {
            usb_printf("[Touch] DOWN X:%u Y:%u G:%u\r\n", x, y, gesture);
        }
    } else {
        block_touch_flag = 0;
        data->state = LV_INDEV_STATE_REL;

        if (prev_pressed) {
            /* 记录滑动手势（左右滑动换页） */
            if (gesture == 0x03 || gesture == 0x04) {
                g_touch_gesture = gesture;
            }
            usb_printf("[Touch] UP last_x:%u last_y:%u\r\n", last_x, last_y);
        }
    }

    prev_pressed = is_pressed;
    data->point.x = last_x;
    data->point.y = last_y;
}

/*-------------------------------------------
 *  Public functions
 *------------------------------------------*/

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
void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    /* 1. 初始化触摸屏硬件 */
    CST816_Init();

    /* 2. 注册输入设备到 LVGL */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read; // 绑定读取回调函数

    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

    /* Optional: Set input device group (if using multiple inputs) */
    /* lv_indev_set_group(indev, group); */

    /* Debug output (can be removed later) */
    // usb_printf("[LVGL] Touch driver initialized\r\n");
}