/**
 * @file app_lvgl.c
 * @brief LVGL V1 ECG Logger UI — Two buttons + file number selector
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
#include "ecg_usb_dump.h"

/* ----- UI objects ----- */
static lv_obj_t *ui_label_title;
static lv_obj_t *ui_label_state;
static lv_obj_t *ui_label_samples;
static lv_obj_t *ui_label_written;
static lv_obj_t *ui_label_drop;
static lv_obj_t *ui_label_file;
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

/* ----- USB Send ECG File button callback ----- */
uint32_t g_usb_info_press_count = 0;

static void btn_usb_send_cb(lv_event_t *e)
{
    (void)e;
    g_usb_info_press_count++;
    ECG_USB_RequestDump();
}

/* ----- File number +/- buttons ----- */
static void btn_file_dec_cb(lv_event_t *e)
{
    (void)e;
    if (g_ecg_rec.file_seq > 1) {
        g_ecg_rec.file_seq--;
        ECG_UpdateFileName();
    }
}

static void btn_file_inc_cb(lv_event_t *e)
{
    (void)e;
    if (g_ecg_rec.file_seq < 999) {
        g_ecg_rec.file_seq++;
        ECG_UpdateFileName();
    }
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

    /* 文件名 */
    lv_label_set_text_fmt(ui_label_file, "ecg_%03lu.csv  ", g_ecg_rec.file_seq);

    /* Start/Stop 按钮 */
    if (g_ecg_rec.state == ECG_REC_RECORDING) {
        lv_label_set_text(ui_btn_start_label, "Stop");
        lv_obj_set_style_bg_color(ui_btn_start, lv_color_hex(0xCC3333), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_label_set_text(ui_btn_start_label, "Start");
        lv_obj_set_style_bg_color(ui_btn_start, lv_color_hex(0x33AA33), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    /* 记录中禁用 USB Send */
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
    lv_obj_t *ui_btn;

    /* Dark background */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1A1A2E), 0);

    /* ---- Title ---- */
    ui_label_title = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_title, "ECG V1 Logger");
    lv_obj_set_style_text_color(ui_label_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(ui_label_title, LV_ALIGN_TOP_MID, 0, 10);

    /* ---- Status ---- */
    ui_label_state = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_state, "Status: IDLE");
    lv_obj_set_style_text_color(ui_label_state, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(ui_label_state, LV_ALIGN_TOP_MID, 0, 32);

    /* ---- Samples / Written / Drop ---- */
    ui_label_samples = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_samples, "Samples: 0");
    lv_obj_set_style_text_color(ui_label_samples, lv_color_hex(0x88CC88), 0);
    lv_obj_align(ui_label_samples, LV_ALIGN_TOP_MID, 0, 50);

    ui_label_written = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_written, "Written: 0");
    lv_obj_set_style_text_color(ui_label_written, lv_color_hex(0x88CC88), 0);
    lv_obj_align(ui_label_written, LV_ALIGN_TOP_MID, 0, 66);

    ui_label_drop = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_drop, "Drop: 0");
    lv_obj_set_style_text_color(ui_label_drop, lv_color_hex(0xCC8888), 0);
    lv_obj_align(ui_label_drop, LV_ALIGN_TOP_MID, 0, 82);

    /* ---- File number row ---- */
    /* "-" button */
    ui_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn, 40, 36);
    lv_obj_align(ui_btn, LV_ALIGN_CENTER, -70, -10);
    lv_obj_set_style_bg_color(ui_btn, lv_color_hex(0x555555), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_btn, 8, 0);
    lv_obj_t *lbl_dec = lv_label_create(ui_btn);
    lv_label_set_text(lbl_dec, "-");
    lv_obj_center(lbl_dec);
    lv_obj_set_style_text_color(lbl_dec, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(ui_btn, btn_file_dec_cb, LV_EVENT_CLICKED, NULL);

    /* filename label */
    ui_label_file = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_file, "ecg_001.csv  ");
    lv_obj_set_style_text_color(ui_label_file, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_label_file, LV_ALIGN_CENTER, 0, -10);

    /* "+" button */
    ui_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn, 40, 36);
    lv_obj_align(ui_btn, LV_ALIGN_CENTER, 70, -10);
    lv_obj_set_style_bg_color(ui_btn, lv_color_hex(0x555555), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_btn, 8, 0);
    lv_obj_t *lbl_inc = lv_label_create(ui_btn);
    lv_label_set_text(lbl_inc, "+");
    lv_obj_center(lbl_inc);
    lv_obj_set_style_text_color(lbl_inc, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(ui_btn, btn_file_inc_cb, LV_EVENT_CLICKED, NULL);

    /* ---- Start/Stop button (center) ---- */
    ui_btn_start = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_start, 160, 48);
    lv_obj_align(ui_btn_start, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(ui_btn_start, lv_color_hex(0x33AA33), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_btn_start, 12, 0);

    ui_btn_start_label = lv_label_create(ui_btn_start);
    lv_label_set_text(ui_btn_start_label, "Start");
    lv_obj_center(ui_btn_start_label);
    lv_obj_set_style_text_color(ui_btn_start_label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_add_event_cb(ui_btn_start, btn_start_cb, LV_EVENT_CLICKED, NULL);

    /* ---- USB Send ECG File button ---- */
    ui_btn_info = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_info, 160, 40);
    lv_obj_align(ui_btn_info, LV_ALIGN_CENTER, 0, 95);
    lv_obj_set_style_bg_color(ui_btn_info, lv_color_hex(0x0077CC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_btn_info, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(ui_btn_info, 12, 0);

    ui_btn_info_label = lv_label_create(ui_btn_info);
    lv_label_set_text(ui_btn_info_label, "USB Send ECG File");
    lv_obj_center(ui_btn_info_label);
    lv_obj_set_style_text_color(ui_btn_info_label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_add_event_cb(ui_btn_info, btn_usb_send_cb, LV_EVENT_CLICKED, NULL);

    /* ---- 500ms 定时更新 UI ---- */
    lv_timer_create(ui_update_cb, 500, NULL);
}

void APP_LVGL_Process(void)
{
    lv_timer_handler();
}
