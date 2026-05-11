/**
 * @file app_lvgl.c
 * @brief LVGL V1 ECG Logger UI — 两页: Main / Diagnostic
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
#include "max3003.h"

/* 触摸手势 — 由 lv_port_indev.c 设置 */
extern volatile uint8_t g_touch_gesture;

/* ----- 页面控制 ----- */
static uint8_t s_page = 0;  /* 0=Main, 1=Diag */

/* ----- Page 1: Main 对象 ----- */
static lv_obj_t *ui_label_title;
static lv_obj_t *ui_label_lead;
static lv_obj_t *ui_label_state;
static lv_obj_t *ui_label_rate;
static lv_obj_t *ui_label_file;
static lv_obj_t *ui_label_samples;
static lv_obj_t *ui_label_drop;
static lv_obj_t *ui_btn_start;
static lv_obj_t *ui_btn_start_label;
static lv_obj_t *ui_btn_info;
static lv_obj_t *ui_btn_info_label;

/* ----- Page 2: Diagnostic 对象 ----- */
static lv_obj_t *ui_label_title2;
static lv_obj_t *ui_label_status_reg;
static lv_obj_t *ui_label_pll_seen;
static lv_obj_t *ui_label_pll_edge;
static lv_obj_t *ui_label_eovf;
static lv_obj_t *ui_label_written;

/* 辅助: 显示/隐藏一组对象 */
static void show_group(lv_obj_t **objs, int count, int visible)
{
    for (int i = 0; i < count; i++) {
        if (visible)
            lv_obj_clear_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }
}

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

/* ----- Save Info button callback ----- */
static void btn_save_info_cb(lv_event_t *e)
{
    (void)e;
    ECG_RequestSaveInfo();
}

/* ----- File number +/- ----- */
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
    static uint32_t last_up = 0;
    static uint32_t last_tick = 0;
    static uint32_t last_fifo = 0;
    static uint32_t rate_hz = 0;
    uint32_t now = HAL_GetTick();

    if (now - last_up < 500) return;
    last_up = now;

    /* ---- 左右滑动换页 ---- */
    if (g_touch_gesture) {
        s_page = !s_page;
        g_touch_gesture = 0;
    }

    /* ---- 页面可见性 ---- */
    /* Page 1 (Main) — 只隐藏顶层容器，子 label 自动跟随 */
    lv_obj_t *page1[] = {
        ui_label_title, ui_label_lead, ui_label_state, ui_label_rate, ui_label_file,
        ui_label_samples, ui_label_drop,
        ui_btn_start, ui_btn_info
    };
    /* Page 2 (Diag) */
    lv_obj_t *page2[] = {
        ui_label_title2, ui_label_status_reg,
        ui_label_pll_seen, ui_label_pll_edge,
        ui_label_eovf, ui_label_written
    };
    show_group(page1, sizeof(page1)/sizeof(page1[0]), s_page == 0);
    show_group(page2, sizeof(page2)/sizeof(page2[0]), s_page == 1);

    /* ---- Page 1 更新 ---- */
    if (s_page == 0) {
        const char *s = "IDLE";
        switch (g_ecg_rec.state) {
            case ECG_REC_IDLE:      s = "IDLE";      break;
            case ECG_REC_RECORDING: s = "RECORDING";  break;
            case ECG_REC_STOPPING:  s = "STOPPING";   break;
            case ECG_REC_STOPPED:   s = "STOPPED";    break;
            case ECG_REC_ERROR:     s = "ERROR";      break;
        }
        lv_label_set_text_fmt(ui_label_state, "State: %s", s);

        /* 电极状态 */
        {
            MAX30003_LeadStatus_t lead;
            MAX30003_GetLeadStatus(&lead);

            if (lead.last_update_ms == 0) {
                lv_label_set_text(ui_label_lead, "\347\224\265\346\236\201: \346\243\200\346\265\213\344\270\255");
                lv_obj_set_style_text_color(ui_label_lead, lv_color_hex(0xFFAA00), 0);
            } else if (lead.state == MAX30003_LEAD_ON) {
                lv_label_set_text(ui_label_lead, "\347\224\265\346\236\201: \345\267\262\350\264\264\345\245\275");
                lv_obj_set_style_text_color(ui_label_lead, lv_color_hex(0x88CC88), 0);
            } else {
                if ((lead.p_high || lead.p_low) && (lead.n_high || lead.n_low)) {
                    lv_label_set_text(ui_label_lead, "\347\224\265\346\236\201: P/N\350\204\261\350\220\275");
                } else if (lead.p_high || lead.p_low) {
                    lv_label_set_text(ui_label_lead, "\347\224\265\346\236\201: P\350\204\261\350\220\275");
                } else if (lead.n_high || lead.n_low) {
                    lv_label_set_text(ui_label_lead, "\347\224\265\346\236\201: N\350\204\261\350\220\275");
                } else {
                    lv_label_set_text(ui_label_lead, "\347\224\265\346\236\201: \350\204\261\350\220\275");
                }
                lv_obj_set_style_text_color(ui_label_lead, lv_color_hex(0xCC3333), 0);
            }
        }

        /* 采样率估算（基于 fifo_sample_count，512Hz 应稳定） */
        if (now - last_tick >= 5000) {
            uint32_t df = g_ecg_rec.fifo_sample_count - last_fifo;
            if (now > last_tick)
                rate_hz = (df * 1000UL) / (now - last_tick);
            last_fifo = g_ecg_rec.fifo_sample_count;
            last_tick = now;
        }
        lv_label_set_text_fmt(ui_label_rate, "Rate: %lu Hz", rate_hz);

        lv_label_set_text_fmt(ui_label_file, "File: ecg_%03lu.csv  ", g_ecg_rec.file_seq);
        lv_label_set_text_fmt(ui_label_samples, "Samples: %lu", g_ecg_rec.ecg_sample_count);
        lv_label_set_text_fmt(ui_label_drop, "Drop: %lu", g_ecg_rec.ecg_drop_count);

        /* Start/Stop 按钮 */
        if (g_ecg_rec.state == ECG_REC_RECORDING) {
            lv_label_set_text(ui_btn_start_label, "Stop");
            lv_obj_set_style_bg_color(ui_btn_start, lv_color_hex(0xCC3333), 0);
        } else {
            lv_label_set_text(ui_btn_start_label, "Start");
            lv_obj_set_style_bg_color(ui_btn_start, lv_color_hex(0x33AA33), 0);
        }

        if (g_ecg_rec.state == ECG_REC_RECORDING) {
            lv_obj_add_state(ui_btn_info, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(ui_btn_info, LV_STATE_DISABLED);
        }
    }

    /* ---- Page 2 更新 ---- */
    if (s_page == 1) {
        {
            MAX30003_LeadStatus_t lead;
            MAX30003_GetLeadStatus(&lead);
            lv_label_set_text_fmt(ui_label_status_reg, "STATUS: 0x%06lX", lead.raw_status);
            lv_label_set_text_fmt(ui_label_pll_seen,   "Lead: P_H%d P_L%d N_H%d N_L%d",
                                  lead.p_high, lead.p_low, lead.n_high, lead.n_low);
        }
        lv_label_set_text_fmt(ui_label_pll_edge,   "PLL edge: %lu",  g_ecg_rec.pll_edge_count);
        lv_label_set_text_fmt(ui_label_eovf,       "EOVF: %lu",      g_ecg_rec.fifo_eovf_count);
        lv_label_set_text_fmt(ui_label_written,    "Written: %lu",   g_ecg_rec.ecg_written_count);
    }
}

/* ----- Public ----- */

void APP_LVGL_Init(void)
{
    DEV_Module_Init();
    LCD_1IN69_SetBackLight(1000);
    LCD_1IN69_Init(VERTICAL);
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    uint8_t chip_id = 0;
    if (CST816_Read_Reg(CST816_REG_CHIP_ID, &chip_id, 1)) {
        usb_printf("[Touch] CST816 Chip ID: 0x%02X\r\n", chip_id);
    }

    lv_obj_clean(lv_scr_act());
    App_LVGL_TestUI();
}

void App_LVGL_TestUI(void)
{
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1A1A2E), 0);

    /* ==================== Page 1: Main ==================== */

    /* Title */
    ui_label_title = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_title, "ECG DATA");
    lv_obj_set_style_text_color(ui_label_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(ui_label_title, LV_ALIGN_TOP_MID, 0, 10);

    /* Lead Status */
    ui_label_lead = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_lead, "\347\224\265\346\236\201: \346\243\200\346\265\213\344\270\255");
    lv_obj_set_style_text_color(ui_label_lead, lv_color_hex(0xFFAA00), 0);
    lv_obj_align(ui_label_lead, LV_ALIGN_TOP_LEFT, 10, 28);

    /* State */
    ui_label_state = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_state, "State: IDLE");
    lv_obj_set_style_text_color(ui_label_state, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(ui_label_state, LV_ALIGN_TOP_LEFT, 10, 44);

    /* Rate */
    ui_label_rate = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_rate, "Rate: 0 Hz");
    lv_obj_set_style_text_color(ui_label_rate, lv_color_hex(0x88CC88), 0);
    lv_obj_align(ui_label_rate, LV_ALIGN_TOP_LEFT, 10, 62);

    /* File */
    ui_label_file = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_file, "File: ecg_001.csv");
    lv_obj_set_style_text_color(ui_label_file, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_label_file, LV_ALIGN_TOP_LEFT, 10, 78);

    /* File +/- buttons */
    lv_obj_t *ui_btn;
    ui_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn, 36, 28);
    lv_obj_align(ui_btn, LV_ALIGN_TOP_LEFT, 10, 94);
    lv_obj_set_style_bg_color(ui_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(ui_btn, 8, 0);
    lv_obj_t *ld = lv_label_create(ui_btn);
    lv_label_set_text(ld, "-");
    lv_obj_center(ld);
    lv_obj_set_style_text_color(ld, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(ui_btn, btn_file_dec_cb, LV_EVENT_CLICKED, NULL);

    ui_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn, 36, 28);
    lv_obj_align(ui_btn, LV_ALIGN_TOP_LEFT, 60, 94);
    lv_obj_set_style_bg_color(ui_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(ui_btn, 8, 0);
    lv_obj_t *li = lv_label_create(ui_btn);
    lv_label_set_text(li, "+");
    lv_obj_center(li);
    lv_obj_set_style_text_color(li, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(ui_btn, btn_file_inc_cb, LV_EVENT_CLICKED, NULL);

    /* Samples */
    ui_label_samples = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_samples, "Samples: 0");
    lv_obj_set_style_text_color(ui_label_samples, lv_color_hex(0x88CC88), 0);
    lv_obj_align(ui_label_samples, LV_ALIGN_TOP_LEFT, 10, 128);

    /* Drop */
    ui_label_drop = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_drop, "Drop: 0");
    lv_obj_set_style_text_color(ui_label_drop, lv_color_hex(0xCC8888), 0);
    lv_obj_align(ui_label_drop, LV_ALIGN_TOP_LEFT, 10, 142);

    /* Start/Stop */
    ui_btn_start = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_start, 160, 44);
    lv_obj_align(ui_btn_start, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_bg_color(ui_btn_start, lv_color_hex(0x33AA33), 0);
    lv_obj_set_style_radius(ui_btn_start, 12, 0);
    ui_btn_start_label = lv_label_create(ui_btn_start);
    lv_label_set_text(ui_btn_start_label, "Start");
    lv_obj_center(ui_btn_start_label);
    lv_obj_set_style_text_color(ui_btn_start_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(ui_btn_start, btn_start_cb, LV_EVENT_CLICKED, NULL);

    /* Save Info */
    ui_btn_info = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_info, 160, 36);
    lv_obj_align(ui_btn_info, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_color(ui_btn_info, lv_color_hex(0x0077CC), 0);
    lv_obj_set_style_bg_color(ui_btn_info, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(ui_btn_info, 12, 0);
    ui_btn_info_label = lv_label_create(ui_btn_info);
    lv_label_set_text(ui_btn_info_label, "Save Info");
    lv_obj_center(ui_btn_info_label);
    lv_obj_set_style_text_color(ui_btn_info_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(ui_btn_info, btn_save_info_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== Page 2: Diagnostic ==================== */

    /* Title */
    ui_label_title2 = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_title2, "ECG Diag");
    lv_obj_set_style_text_color(ui_label_title2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_label_title2, &lv_font_montserrat_14, 0);
    lv_obj_align(ui_label_title2, LV_ALIGN_TOP_MID, 0, 10);

    /* Diag rows */
    int y = 40;
    int dy = 16;

    ui_label_status_reg = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_status_reg, "STATUS: 0x000000");
    lv_obj_set_style_text_color(ui_label_status_reg, lv_color_hex(0x44AAFF), 0);
    lv_obj_align(ui_label_status_reg, LV_ALIGN_TOP_LEFT, 10, y); y += dy;

    ui_label_pll_seen = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_pll_seen, "PLL seen: 0");
    lv_obj_set_style_text_color(ui_label_pll_seen, lv_color_hex(0xCCAA44), 0);
    lv_obj_align(ui_label_pll_seen, LV_ALIGN_TOP_LEFT, 10, y); y += dy;

    ui_label_pll_edge = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_pll_edge, "PLL edge: 0");
    lv_obj_set_style_text_color(ui_label_pll_edge, lv_color_hex(0xCCAA44), 0);
    lv_obj_align(ui_label_pll_edge, LV_ALIGN_TOP_LEFT, 10, y); y += dy;

    ui_label_eovf = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_eovf, "EOVF: 0");
    lv_obj_set_style_text_color(ui_label_eovf, lv_color_hex(0xCCAA44), 0);
    lv_obj_align(ui_label_eovf, LV_ALIGN_TOP_LEFT, 10, y); y += dy;

    ui_label_written = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_label_written, "Written: 0");
    lv_obj_set_style_text_color(ui_label_written, lv_color_hex(0x88CC88), 0);
    lv_obj_align(ui_label_written, LV_ALIGN_TOP_LEFT, 10, y); y += dy;

    /* 默认显示 Page 1, 隐藏 Page 2 */
    lv_obj_t *hide_init[] = {
        ui_label_title2, ui_label_status_reg, ui_label_pll_seen,
        ui_label_pll_edge, ui_label_eovf, ui_label_written
    };
    for (size_t i = 0; i < sizeof(hide_init)/sizeof(hide_init[0]); i++) {
        lv_obj_add_flag(hide_init[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_timer_create(ui_update_cb, 500, NULL);
}

void APP_LVGL_Process(void)
{
    lv_timer_handler();
}
