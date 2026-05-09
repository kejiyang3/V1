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
    .test_mode = ECG_TEST_OPEN_INPUT,
    .file_seq = 0,
    .fifo_eovf_count = 0,
    .pll_warn_count = 0,
    .last_status = 0,
    .pll_status_seen_count = 0,
    .pll_edge_count = 0,
    .pll_current_set = 0,
    .fifo_sample_count = 0,
    .fifo_empty_count = 0,
    .fifo_etag_overflow_count = 0,
    .file_name = "0:/ecg_open_000.csv"
};

void ECG_UpdateFileNameForNewRecording(void)
{
    g_ecg_rec.file_seq++;

    switch (g_ecg_rec.test_mode) {
        case ECG_TEST_OPEN_INPUT:
            snprintf(g_ecg_rec.file_name, sizeof(g_ecg_rec.file_name),
                     "0:/ecg_open_%03lu.csv", g_ecg_rec.file_seq);
            break;
        case ECG_TEST_NEAR_SHORT:
            snprintf(g_ecg_rec.file_name, sizeof(g_ecg_rec.file_name),
                     "0:/ecg_short_%03lu.csv", g_ecg_rec.file_seq);
            break;
        case ECG_TEST_HUMAN_BODY:
            snprintf(g_ecg_rec.file_name, sizeof(g_ecg_rec.file_name),
                     "0:/ecg_human_%03lu.csv", g_ecg_rec.file_seq);
            break;
        default:
            snprintf(g_ecg_rec.file_name, sizeof(g_ecg_rec.file_name),
                     "0:/ecg_open_%03lu.csv", g_ecg_rec.file_seq);
            break;
    }
}

void ECG_RequestStart(void)
{
    ECG_UpdateFileNameForNewRecording();
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

void ECG_ResetStats(void)
{
    g_ecg_rec.ecg_sample_count = 0;
    g_ecg_rec.ecg_written_count = 0;
    g_ecg_rec.ecg_drop_count = 0;
    g_ecg_rec.sd_write_bytes = 0;
    g_ecg_rec.sd_sync_count = 0;

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

void ECG_SetTestMode(ECG_TestMode_t mode)
{
    if (g_ecg_rec.state == ECG_REC_RECORDING ||
        g_ecg_rec.state == ECG_REC_STOPPING) {
        return;
    }
    g_ecg_rec.test_mode = mode;
}

void ECG_ToggleTestMode(void)
{
    if (g_ecg_rec.state == ECG_REC_RECORDING ||
        g_ecg_rec.state == ECG_REC_STOPPING) {
        return;
    }
    switch (g_ecg_rec.test_mode) {
        case ECG_TEST_OPEN_INPUT:
            g_ecg_rec.test_mode = ECG_TEST_NEAR_SHORT;
            break;
        case ECG_TEST_NEAR_SHORT:
            g_ecg_rec.test_mode = ECG_TEST_HUMAN_BODY;
            break;
        case ECG_TEST_HUMAN_BODY:
        default:
            g_ecg_rec.test_mode = ECG_TEST_OPEN_INPUT;
            break;
    }
}

const char *ECG_GetTestModeName(void)
{
    switch (g_ecg_rec.test_mode) {
        case ECG_TEST_OPEN_INPUT: return "OPEN";
        case ECG_TEST_NEAR_SHORT: return "SHORT";
        case ECG_TEST_HUMAN_BODY: return "HUMAN";
        default:                  return "UNKNOWN";
    }
}
