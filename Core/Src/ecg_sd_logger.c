#include "ecg_sd_logger.h"
#include "ecg_record_control.h"
#include "sd_debug_log.h"
#include "fatfs.h"
#include "ff.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#include "app_log.h"
extern osMutexId_t Mtx_SDCardHandle;

#define ECG_SD_QUEUE_DEPTH      1024
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

    APP_USB_LOG("[ECG_SD] OpenFile: begin\r\n");

    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(2000)) != osOK) {
            APP_USB_LOG("[ECG_SD][ERR] SD mutex acquire timeout\r\n");
            return FR_INT_ERR;
        }
    }

    APP_USB_LOG("[ECG_SD] OpenFile: before f_mount\r\n");
    res = f_mount(&SDFatFS, SDPath, 1);
    APP_USB_LOG("[ECG_SD] OpenFile: f_mount res=%d\r\n", res);

    if (res != FR_OK) {
        if (Mtx_SDCardHandle != NULL) {
            osMutexRelease(Mtx_SDCardHandle);
        }
        return res;
    }

    APP_USB_LOG("[ECG_SD] OpenFile: before f_open %s\r\n", g_ecg_rec.file_name);
    res = f_open(&s_ecg_file, g_ecg_rec.file_name, FA_CREATE_ALWAYS | FA_WRITE);
    APP_USB_LOG("[ECG_SD] OpenFile: f_open res=%d\r\n", res);

    if (res != FR_OK) {
        if (Mtx_SDCardHandle != NULL) {
            osMutexRelease(Mtx_SDCardHandle);
        }
        return res;
    }

    const char *header = "timestamp_ms,seq,ecg\r\n";

    APP_USB_LOG("[ECG_SD] OpenFile: before header write\r\n");
    res = f_write(&s_ecg_file, header, strlen(header), &bw);
    APP_USB_LOG("[ECG_SD] OpenFile: header write res=%d bw=%lu\r\n", res, (uint32_t)bw);

    if (res == FR_OK) {
        g_ecg_rec.sd_write_bytes += bw;
    }

    APP_USB_LOG("[ECG_SD] OpenFile: before f_sync\r\n");
    FRESULT sync_res = f_sync(&s_ecg_file);
    APP_USB_LOG("[ECG_SD] OpenFile: f_sync res=%d\r\n", sync_res);

    if (res == FR_OK && sync_res != FR_OK) {
        res = sync_res;
    }

    s_file_opened = (res == FR_OK) ? 1 : 0;
    if (s_file_opened) {
        g_ecg_rec.sd_file_opened = 1;
        g_ecg_rec.sd_file_closed = 0;
    }

    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    APP_USB_LOG("[ECG_SD] OpenFile: end res=%d\r\n", res);

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
    g_ecg_rec.sd_file_closed = 1;

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

    APP_USB_LOG("[ECG_SD] task entered\r\n");
    APP_USB_LOG("[ECG_SD] queue ready\r\n");

    uint8_t waiting_printed = 0;

    for (;;) {
        waiting_printed = 0;

        /* 等待 Start 请求进入 RECORDING 状态 */
        while (g_ecg_rec.state != ECG_REC_RECORDING) {
            if (!waiting_printed) {
                APP_USB_LOG("[ECG_SD] waiting for RECORDING state, current=%d\r\n",
                                g_ecg_rec.state);
                waiting_printed = 1;
            }
            osDelay(50);
        }

        APP_USB_LOG("[ECG_SD] RECORDING detected, opening file\r\n");

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
            APP_USB_LOG("[ECG_SD][ERR] open failed res=%d\r\n", res);
            continue;
        }

        APP_USB_LOG("[ECG_SD] recording file opened: %s\r\n", g_ecg_rec.file_name);

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

        SD_DebugLog_WriteLine("CAL_TEST_STOPPED");
        SD_DebugLog_WriteSnapshot();

        APP_USB_LOG("[ECG_SD] stopped. samples=%lu written=%lu drop=%lu bytes=%lu\r\n",
                        g_ecg_rec.ecg_sample_count,
                        g_ecg_rec.ecg_written_count,
                        g_ecg_rec.ecg_drop_count,
                        g_ecg_rec.sd_write_bytes);
    }
}
