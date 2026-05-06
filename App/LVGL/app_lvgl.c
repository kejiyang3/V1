/**
 * @file app_lvgl.c
 * @brief LVGL application layer implementation
 * @note This file provides the minimal LVGL integration for STM32L496.
 *       Display and input drivers are implemented and tested.
 */

#include "app_lvgl.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "main.h"  /* for HAL_GetTick() and GPIO definitions */
#include "stm32l4xx_hal.h" /* Ensure HAL_GetTick is available */
#include "DEV_Config.h"
#include "LCD_1in69.h"
#include "../../User/touch.h"  /* for g_touch touch data structure */
#include "../../User/Config/usb_printf.h"  /* for usb_printf -> USB CDC output */

/* LVGL → FreeRTOS ECG dump interface (defined in freertos.c) */
extern uint32_t APP_LVGL_GetBufferCount(void);
extern uint8_t  APP_LVGL_IsDumping(void);
extern void    APP_LVGL_TriggerEcgDump(void);


/*-------------------------------------------
 *  ECG Export UI — 屏幕按钮替代物理按键
 *------------------------------------------*/

static lv_obj_t *ui_ecg_btn;
static lv_obj_t *ui_ecg_btn_label;
static lv_obj_t *ui_ecg_status;
static lv_obj_t *ui_ecg_feedback;

/* 按钮点击 → 触发 USB dump */
extern void Safe_USB_Printf(const char *format, ...);

static void ecg_export_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        uint32_t count = APP_LVGL_GetBufferCount();
        Safe_USB_Printf("[LVGL] Button clicked! Buffer has %lu samples\r\n", count);
        APP_LVGL_TriggerEcgDump();
    }
}

/* 200ms 定时更新：buffer 计数 + 按钮状态 */
static void ui_ecg_update_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t count = APP_LVGL_GetBufferCount();
    uint8_t dumping = APP_LVGL_IsDumping();

    lv_label_set_text_fmt(ui_ecg_status, "Buffer: %lu / 10240", count);

    if (dumping) {
        lv_obj_add_state(ui_ecg_btn, LV_STATE_DISABLED);
        lv_label_set_text(ui_ecg_btn_label, "Dumping...");
        lv_label_set_text(ui_ecg_feedback, "Sending via USB...");
    } else if (count > 0) {
        lv_obj_clear_state(ui_ecg_btn, LV_STATE_DISABLED);
        lv_label_set_text(ui_ecg_btn_label, "Export ECG");
        lv_label_set_text_fmt(ui_ecg_feedback, "Ready. %lu samples", count);
    } else {
        lv_obj_add_state(ui_ecg_btn, LV_STATE_DISABLED);
        lv_label_set_text(ui_ecg_btn_label, "Export ECG");
        lv_label_set_text(ui_ecg_feedback, "No data yet");
    }
}

/*-------------------------------------------
 *  Static variables for UI components
 *------------------------------------------*/
// UI simplified - no static variables needed

/*-------------------------------------------
 *  Public functions
 *------------------------------------------*/

/**
 * @brief Initialize LVGL core and prepare for display/input drivers
 *
 * This function performs the following steps:
 * 1. Initialize LVGL core library
 * 2. Register display driver with flush callback (DMA enabled)
 * 3. Register input device driver (CST816 touch)
 * 4. Set up tick source (1ms interrupt via HAL_GetTick())
 * 5. Create sleep monitor wearable demo UI with 3 tabs
 *
 * @note Display driver registration must happen before creating any UI objects.
 *       The flush callback will be called by LVGL when a screen area needs updating.
 */
void APP_LVGL_Init(void)
{
    /* Initialize LCD hardware (GPIO, SPI, PWM backlight) */
    DEV_Module_Init();
    LCD_1IN69_SetBackLight(1000);
    LCD_1IN69_Init(VERTICAL);

    /* Init LVGL Core */
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* Quick sanity check: CST816 already initialized in lv_port_indev_init() */
    uint8_t chip_id = 0;
    if (CST816_Read_Reg(CST816_REG_CHIP_ID, &chip_id, 1)) {
        usb_printf("[Touch] CST816 Chip ID: 0x%02X\r\n", chip_id);
    }

    lv_obj_clean(lv_scr_act()); /* Clear screen */

    /* Create test UI */
    App_LVGL_TestUI();
}

/**
 * @brief Create a touch toggle color demo UI
 *
 * Creates a 2x2 grid of toggle buttons on a dark background.
 * Each button toggles between Red (OFF) and Green (ON) when touched.
 * A debug label at top-left shows real-time touch coordinates.
 */
void App_LVGL_TestUI(void)
{
    /* Dark background */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1A1A2E), 0);

    /* ---- Title ---- */
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "ECG Monitor");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* ---- Buffer fill status ---- */
    ui_ecg_status = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(ui_ecg_status, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(ui_ecg_status, "Buffer: 0 / 10240");
    lv_obj_align(ui_ecg_status, LV_ALIGN_TOP_MID, 0, 50);

    /* ---- Export button (160 x 72, centered) ---- */
    ui_ecg_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_ecg_btn, 160, 72);
    lv_obj_align(ui_ecg_btn, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(ui_ecg_btn, lv_color_hex(0x0077CC),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_ecg_btn, lv_color_hex(0x005599),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui_ecg_btn, lv_color_hex(0x444444),
                              LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(ui_ecg_btn, 12, 0);

    ui_ecg_btn_label = lv_label_create(ui_ecg_btn);
    lv_label_set_text(ui_ecg_btn_label, "Export ECG");
    lv_obj_center(ui_ecg_btn_label);
    lv_obj_set_style_text_color(ui_ecg_btn_label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_add_event_cb(ui_ecg_btn, ecg_export_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(ui_ecg_btn, LV_STATE_DISABLED);  /* 初始无数据，禁用 */

    /* ---- Feedback label ---- */
    ui_ecg_feedback = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(ui_ecg_feedback, lv_color_hex(0x88CC88), 0);
    lv_label_set_text(ui_ecg_feedback, "No data yet");
    lv_obj_align(ui_ecg_feedback, LV_ALIGN_CENTER, 0, 60);

    /* ---- 200ms 定时更新 buffer 状态 ---- */
    lv_timer_create(ui_ecg_update_cb, 200, NULL);
}

/**
 * @brief Process LVGL tasks (timer handler)
 *
 * This function should be called periodically in the main loop.
 * It handles:
 * - LVGL internal timers
 * - Display refresh scheduling
 * - Input device polling
 *
 * @note Do not call HAL_Delay() or blocking functions here.
 *       Keep this function non-blocking.
 */
void APP_LVGL_Process(void)
{
    /* LVGL timer handler — called periodically from Task_LVGL */
    lv_timer_handler();
}