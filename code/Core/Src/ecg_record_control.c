#include "ecg_record_control.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

ECG_RecordControl_t g_ecg_rec = {
    .state = ECG_REC_IDLE,
    .request_start = 0,
    .request_stop = 0,
    .request_usb_info = 0,
    .request_save_info = 0,
    .sd_file_opened = 0,
    .sd_file_closed = 1,
    .file_seq = 1,
    .fifo_eovf_count = 0,
    .pll_warn_count = 0,
    .last_status = 0,
    .pll_status_seen_count = 0,
    .pll_edge_count = 0,
    .pll_current_set = 0,
    .fifo_sample_count = 0,
    .fifo_empty_count = 0,
    .fifo_etag_overflow_count = 0,
    .file_name = "0:/ecg_001.csv"
};

/* 根据当前 file_seq 更新 file_name */
void ECG_UpdateFileName(void)
{
    snprintf(g_ecg_rec.file_name, sizeof(g_ecg_rec.file_name),
             "0:/ecg_%03lu.csv", g_ecg_rec.file_seq);
}

void ECG_RequestStart(void)
{
    ECG_UpdateFileName();
    g_ecg_rec.request_start = 1;
}

void ECG_RequestStop(void)
{
    g_ecg_rec.request_stop = 1;
}

void ECG_RequestUsbInfo(void)
{
    g_ecg_rec.request_usb_info = 1;
}

void ECG_RequestSaveInfo(void)
{
    g_ecg_rec.request_save_info = 1;
}

/* 每次开始新记录前重置所有统计 */
void ECG_ResetStats(void)
{
    g_ecg_rec.last_status = 0;

    g_ecg_rec.pll_warn_count = 0;
    g_ecg_rec.pll_status_seen_count = 0;
    g_ecg_rec.pll_edge_count = 0;
    g_ecg_rec.pll_current_set = 0;

    g_ecg_rec.fifo_sample_count = 0;
    g_ecg_rec.fifo_eovf_count = 0;
    g_ecg_rec.fifo_empty_count = 0;
    g_ecg_rec.fifo_etag_overflow_count = 0;
}
