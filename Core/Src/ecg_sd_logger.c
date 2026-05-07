#include "ecg_sd_logger.h"
#include "ecg_record_control.h"
#include "fatfs.h"
#include "ff.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern void Safe_USB_Printf(const char *format, ...);
extern osMutexId_t Mtx_SDCardHandle;

#define ECG_SD_QUEUE_DEPTH      2048
#define ECG_SD_SYNC_EVERY       512
#define ECG_SD_LINE_BUF_SIZE    64

osMessageQueueId_t Q_ECG_SDHandle = NULL;

static FIL s_ecg_file;
static uint8_t s_file_opened = 0;
static volatile uint8_t s_stop_requested = 0;
static uint32_t s_ecg_seq = 0;

void ECG_SDLogger_InitQueue(void)
{
    if (Q_ECG_SDHandle == NULL) {
        Q_ECG_SDHandle = osMessageQueueNew(
            ECG_SD_QUEUE_DEPTH,
            sizeof(ECG_SD_Record_t),
            NULL
        );
    }
}

void ECG_SDLogger_RequestStop(void)
{
    s_stop_requested = 1;
}

void ECG_SDLogger_Enqueue(int16_t ecg)
{
    if (Q_ECG_SDHandle == NULL) {
        return;
    }

    if (g_ecg_rec.state != ECG_REC_RECORDING) {
        return;
    }

    ECG_SD_Record_t rec;
    rec.timestamp_ms = HAL_GetTick();
    rec.seq = s_ecg_seq++;
    rec.ecg = ecg;

    g_ecg_rec.ecg_sample_count++;

    if (osMessageQueuePut(Q_ECG_SDHandle, &rec, 0, 0) != osOK) {
        g_ecg_rec.ecg_drop_count++;
    }
}

static FRESULT ECG_SDLogger_OpenFile(void)
{
    FRESULT res;
    UINT bw = 0;

    if (Mtx_SDCardHandle != NULL) {
        osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    }

    res = f_mount(&SDFatFS, SDPath, 1);
    if (res != FR_OK) {
        if (Mtx_SDCardHandle != NULL) {
            osMutexRelease(Mtx_SDCardHandle);
        }
        return res;
    }

    res = f_open(&s_ecg_file, g_ecg_rec.file_name, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        if (Mtx_SDCardHandle != NULL) {
            osMutexRelease(Mtx_SDCardHandle);
        }
        return res;
    }

    const char *header = "timestamp_ms,seq,ecg\r\n";
    res = f_write(&s_ecg_file, header, strlen(header), &bw);
    if (res == FR_OK) {
        g_ecg_rec.sd_write_bytes += bw;
    }

    f_sync(&s_ecg_file);
    s_file_opened = 1;

    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    return res;
}

static void ECG_SDLogger_CloseFile(void)
{
    if (!s_file_opened) {
        return;
    }

    if (Mtx_SDCardHandle != NULL) {
        osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    }

    f_sync(&s_ecg_file);
    f_close(&s_ecg_file);
    f_mount(NULL, SDPath, 1);

    s_file_opened = 0;

    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }
}

void StartTask_ECG_SDWriter(void *argument)
{
    (void)argument;

    ECG_SD_Record_t rec;
    char line[ECG_SD_LINE_BUF_SIZE];
    UINT bw = 0;
    FRESULT res;
    uint32_t local_written = 0;

    ECG_SDLogger_InitQueue();

    for (;;) {
        /* 等待 Start 请求进入 RECORDING 状态 */
        while (g_ecg_rec.state != ECG_REC_RECORDING) {
            osDelay(50);
        }

        s_stop_requested = 0;
        s_ecg_seq = 0;
        local_written = 0;

        g_ecg_rec.ecg_sample_count = 0;
        g_ecg_rec.ecg_written_count = 0;
        g_ecg_rec.ecg_drop_count = 0;
        g_ecg_rec.sd_write_bytes = 0;
        g_ecg_rec.sd_sync_count = 0;
        g_ecg_rec.start_tick = HAL_GetTick();

        res = ECG_SDLogger_OpenFile();
        if (res != FR_OK) {
            g_ecg_rec.state = ECG_REC_ERROR;
            Safe_USB_Printf("[ECG_SD][ERR] open failed res=%d\r\n", res);
            continue;
        }

        Safe_USB_Printf("[ECG_SD] recording file opened: %s\r\n", g_ecg_rec.file_name);

        while (g_ecg_rec.state == ECG_REC_RECORDING || osMessageQueueGetCount(Q_ECG_SDHandle) > 0) {
            if (osMessageQueueGet(Q_ECG_SDHandle, &rec, NULL, pdMS_TO_TICKS(50)) == osOK) {
                int n = snprintf(line, sizeof(line), "%lu,%lu,%d\r\n",
                                 rec.timestamp_ms, rec.seq, rec.ecg);

                if (n > 0) {
                    if (Mtx_SDCardHandle != NULL) {
                        osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
                    }

                    bw = 0;
                    res = f_write(&s_ecg_file, line, (UINT)n, &bw);

                    if (res == FR_OK && bw == (UINT)n) {
                        local_written++;
                        g_ecg_rec.ecg_written_count = local_written;
                        g_ecg_rec.sd_write_bytes += bw;
                    } else {
                        g_ecg_rec.ecg_drop_count++;
                    }

                    if ((local_written % ECG_SD_SYNC_EVERY) == 0) {
                        f_sync(&s_ecg_file);
                        g_ecg_rec.sd_sync_count++;
                    }

                    if (Mtx_SDCardHandle != NULL) {
                        osMutexRelease(Mtx_SDCardHandle);
                    }
                }
            }

            if (s_stop_requested || g_ecg_rec.request_stop) {
                g_ecg_rec.state = ECG_REC_STOPPING;
            }
        }

        ECG_SDLogger_CloseFile();

        g_ecg_rec.stop_tick = HAL_GetTick();
        g_ecg_rec.state = ECG_REC_STOPPED;
        g_ecg_rec.request_stop = 0;

        Safe_USB_Printf("[ECG_SD] stopped. samples=%lu written=%lu drop=%lu bytes=%lu\r\n",
                        g_ecg_rec.ecg_sample_count,
                        g_ecg_rec.ecg_written_count,
                        g_ecg_rec.ecg_drop_count,
                        g_ecg_rec.sd_write_bytes);
    }
}
