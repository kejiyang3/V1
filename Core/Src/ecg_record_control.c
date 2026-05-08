#include "ecg_record_control.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

ECG_RecordControl_t g_ecg_rec = {
    .state = ECG_REC_IDLE,
    .request_start = 0,
    .request_stop = 0,
    .request_usb_info = 0,
    .sd_file_opened = 0,
    .sd_file_closed = 1,
    .file_seq = 1,
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
