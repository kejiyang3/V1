/**
 * @file app_lvgl.c
 * @brief LVGL V1 ECG Logger UI — Two buttons
 */

#include "app_lvgl.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "main.h"
#include "stm32l4xx_hal.h"
#include "DEV_Config.h"
#include "LCD_1in69.h"
#include "../../User/touch.h"
#include "../../User/Config/usb_printf.h"
#include "ecg_record_control.h"

/* ----- UI objects ----- */
static lv_obj_t *ui_label_title;
static lv_obj_t *ui_label_state;
static lv_obj_t *ui_label_samples;
static lv_obj_t *ui_label_written;
static lv_obj_t *ui_label_drop;
static lv_obj_t *ui_btn_start;
static lv_obj_t *ui_btn_start_label;
static lv_obj_t *ui_btn_info;
static lv_obj_t *ui_btn_info_label;

/* ----- Start/Stop button callback ----- */
static void btn_start_cb(lv_event_t *e)
{
    (void)e;
    if (g_ecg_rec.state == ECG_REC_IDLE ||
        g_ecg_rec.state == ECG_REC_STOPPED ||
        g_ecg_rec.state == ECG_REC_ERROR) {
        ECG_RequestStart();
    } else if (g_ecg_rec.state == ECG_REC_RECORDING) {
        ECG_RequestStop();
    }
}

/* ----- USB Info button callback ----- */
static void btn_info_cb(lv_event_t *e)
{
    (void)e;
    ECG_RequestUsbInfo();
}

/* ----- UI update timer (500ms) ----- */
static void ui_update_cb(lv_timer_t *timer)
{
    (void)timer;
    static uint32_t last_update = 0;
    if (HAL_GetTick() - last_update < 500) return;
    last_update = HAL_GetTick();

    /* 状态文本 */
    const char *state_str = "IDLE";
    switch (g_ecg_rec.state) {
        case ECG_REC_IDLE:      state_str = "IDLE";      break;
        case ECG_REC_RECORDING: state_str = "RECORDING";  break;
        case ECG_REC_STOPPING:  state_str = "STOPPING";   break;
        case ECG_REC_STOPPED:   state_str = "STOPPED";    break;
        case ECG_REC_ERROR:     state_str = "ERROR";      break;
    }
    lv_label_set_text_fmt(ui_label_state, "Status: %s", state_str);

    /* 样本统计 */
    lv_label_set_text_fmt(ui_label_samples, "Samples: %lu", g_ecg_rec.ecg_sample_count);
    lv_label_set_text_fmt(ui_label_written, "Written: %lu", g_ecg_rec.ecg_written_count);
    lv_label_set_text_fmt(ui_label_drop,    "Drop: %lu",    g_ecg_rec.ecg_drop_count);

    /* Start/Stop 按钮状态 */
    if (g_ecg_rec.state == ECG_REC_RECORDING) {
        lv_label_set_text(ui_btn_start_label, "Stop");
        lv_obj_set_style_bg_color(ui_btn_start, lv_color_hex(0xCC3333), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_label_set_text(ui_btn_start_label, "Start");
        lv_obj_set_style_bg_color(ui_btn_start, lv_color_hex(0x33AA33), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    /* 记录中禁用 USB Info 按钮 */
    if (g_ecg_rec.state == ECG_REC_RECORDING) {
        lv_obj_add_state(ui_btn_info, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(ui_btn_info, LV_STATE_DISABLED);
    }
}

/* ----- Public functions ----- */

void APP_LVGL_Init(void)
{
    DEV_Module_Init();
    LCD_1IN69_SetBackLight(1000);
    LCD_1IN69_Init(VERTICAL);

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* Touch sanity check */
    uint8_t chip_id = 0;
    if (CST816_Read_Reg(CST816_REG_CHIP_ID, &chip_id, 1)) {
        usb_printf("[Touch] CST816 Chip ID: 0x%02X\r\n", chip_id);
    }

    lv_obj_clean(lv_scr_act());
    App_LVGL_TestUI();
}

void App_LVGL_TestUI(void)
{
    /* Dark background */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1A1A2E), 0);

    /* ---- Title ---- */
    ui_label_title = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_title, "ECG V1 Logger");
    lv_obj_set_style_text_color(ui_label_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(ui_label_title, LV_ALIGN_TOP_MID, 0, 15);

    /* ---- Status ---- */
    ui_label_state = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_state, "Status: IDLE");
    lv_obj_set_style_text_color(ui_label_state, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(ui_label_state, LV_ALIGN_TOP_MID, 0, 40);

    /* ---- Samples ---- */
    ui_label_samples = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_samples, "Samples: 0");
    lv_obj_set_style_text_color(ui_label_samples, lv_color_hex(0x88CC88), 0);
    lv_obj_align(ui_label_samples, LV_ALIGN_TOP_MID, 0, 60);

    /* ---- Written ---- */
    ui_label_written = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_written, "Written: 0");
    lv_obj_set_style_text_color(ui_label_written, lv_color_hex(0x88CC88), 0);
    lv_obj_align(ui_label_written, LV_ALIGN_TOP_MID, 0, 78);

    /* ---- Drop ---- */
    ui_label_drop = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_drop, "Drop: 0");
    lv_obj_set_style_text_color(ui_label_drop, lv_color_hex(0xCC8888), 0);
    lv_obj_align(ui_label_drop, LV_ALIGN_TOP_MID, 0, 96);

    /* ---- Start/Stop button (center) ---- */
    ui_btn_start = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_start, 160, 56);
    lv_obj_align(ui_btn_start, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(ui_btn_start, lv_color_hex(0x33AA33), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_btn_start, 12, 0);

    ui_btn_start_label = lv_label_create(ui_btn_start);
    lv_label_set_text(ui_btn_start_label, "Start");
    lv_obj_center(ui_btn_start_label);
    lv_obj_set_style_text_color(ui_btn_start_label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_add_event_cb(ui_btn_start, btn_start_cb, LV_EVENT_CLICKED, NULL);

    /* ---- USB Info button (below) ---- */
    ui_btn_info = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_info, 160, 44);
    lv_obj_align(ui_btn_info, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(ui_btn_info, lv_color_hex(0x0077CC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_btn_info, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(ui_btn_info, 12, 0);

    ui_btn_info_label = lv_label_create(ui_btn_info);
    lv_label_set_text(ui_btn_info_label, "USB Print Info");
    lv_obj_center(ui_btn_info_label);
    lv_obj_set_style_text_color(ui_btn_info_label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_add_event_cb(ui_btn_info, btn_info_cb, LV_EVENT_CLICKED, NULL);

    /* ---- 500ms 定时更新 UI ---- */
    lv_timer_create(ui_update_cb, 500, NULL);
}

void APP_LVGL_Process(void)
{
    lv_timer_handler();
}
