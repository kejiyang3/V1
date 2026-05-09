#ifndef __ECG_RECORD_CONTROL_H__
#define __ECG_RECORD_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    ECG_REC_IDLE = 0,
    ECG_REC_RECORDING,
    ECG_REC_STOPPING,
    ECG_REC_STOPPED,
    ECG_REC_ERROR
} ECG_RecordState_t;

typedef enum {
    ECG_TEST_OPEN_INPUT = 0,
    ECG_TEST_NEAR_SHORT = 1,
    ECG_TEST_HUMAN_BODY = 2
} ECG_TestMode_t;

typedef struct {
    volatile ECG_RecordState_t state;
    volatile uint8_t request_start;
    volatile uint8_t request_stop;
    volatile uint8_t request_usb_info;

    volatile uint32_t start_tick;
    volatile uint32_t stop_tick;

    volatile uint32_t ecg_sample_count;
    volatile uint32_t ecg_written_count;
    volatile uint32_t ecg_drop_count;
    volatile uint32_t sd_write_bytes;
    volatile uint32_t sd_sync_count;
    volatile uint32_t fifo_eovf_count;
    volatile uint32_t pll_warn_count;

    /* STATUS / PLL / FIFO 详细诊断 */
    volatile uint32_t last_status;
    volatile uint32_t pll_status_seen_count;
    volatile uint32_t pll_edge_count;
    volatile uint8_t  pll_current_set;
    volatile uint32_t fifo_sample_count;
    volatile uint32_t fifo_empty_count;
    volatile uint32_t fifo_etag_overflow_count;

    volatile uint8_t sd_file_opened;
    volatile uint8_t sd_file_closed;

    volatile uint8_t request_save_info;

    volatile ECG_TestMode_t test_mode;
    volatile uint32_t file_seq;
    char file_name[32];
} ECG_RecordControl_t;

extern ECG_RecordControl_t g_ecg_rec;

void ECG_RequestStart(void);
void ECG_RequestStop(void);
void ECG_RequestUsbInfo(void);
void ECG_RequestSaveInfo(void);
void ECG_UpdateFileName(void);
void ECG_ResetStats(void);

void ECG_SetTestMode(ECG_TestMode_t mode);
void ECG_ToggleTestMode(void);
const char *ECG_GetTestModeName(void);
void ECG_UpdateFileNameForNewRecording(void);

#ifdef __cplusplus
}
#endif

#endif /* __ECG_RECORD_CONTROL_H__ */
