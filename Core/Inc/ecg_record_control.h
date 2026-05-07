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

    char file_name[32];
} ECG_RecordControl_t;

extern ECG_RecordControl_t g_ecg_rec;

void ECG_RequestStart(void);
void ECG_RequestStop(void);
void ECG_RequestUsbInfo(void);

#ifdef __cplusplus
}
#endif

#endif /* __ECG_RECORD_CONTROL_H__ */
