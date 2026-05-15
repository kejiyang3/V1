#include "sd_sensor_logger.h"
#include "fatfs.h"
#include "ff.h"
#include "usb_printf.h"
#include <stdio.h>
#include <string.h>

extern osMutexId_t Mtx_SDCardHandle;
extern void Safe_USB_Printf(const char *format, ...);

#define SENSOR_QUEUE_DEPTH       1024
#define SD_LOG_FILE_PATH         "0:/sas_log.csv"
#define SD_WRITE_LINE_BUF_SIZE   128
#define SD_SYNC_EVERY_RECORDS    256

osMessageQueueId_t Q_SensorRecordHandle = NULL;

static FIL s_log_file;
static uint32_t s_written_records = 0;
static uint32_t s_dropped_records = 0;

void SDLogger_InitQueue(void)
{
    if (Q_SensorRecordHandle == NULL) {
        Q_SensorRecordHandle = osMessageQueueNew(
            SENSOR_QUEUE_DEPTH,
            sizeof(SensorRecord_t),
            NULL
        );
    }
}

void SDLogger_EnqueueFromTask(const SensorRecord_t *rec)
{
    if (rec == NULL || Q_SensorRecordHandle == NULL) {
        return;
    }

    if (osMessageQueuePut(Q_SensorRecordHandle, rec, 0, 0) != osOK) {
        s_dropped_records++;
        if ((s_dropped_records <= 5) || ((s_dropped_records % 100) == 0)) {
            Safe_USB_Printf("[SDLOG][WARN] queue full, dropped=%lu\r\n", s_dropped_records);
        }
    }
}

static const char *SDLogger_TypeToString(SensorRecordType_t type)
{
    switch (type) {
        case SENSOR_REC_ECG: return "ECG";
        case SENSOR_REC_PPG: return "PPG";
        case SENSOR_REC_IMU: return "IMU";
        default: return "UNK";
    }
}

static int SDLogger_RecordToCsvLine(const SensorRecord_t *rec, char *buf, size_t len)
{
    if (rec == NULL || buf == NULL || len == 0) return 0;

    switch (rec->type) {
    case SENSOR_REC_ECG:
        return snprintf(buf, len,
            "%lu,ECG,%lu,%d,0,0,0,0,0,0,0,0\r\n",
            rec->timestamp_ms, rec->seq, rec->data.ecg.ecg);

    case SENSOR_REC_PPG:
        return snprintf(buf, len,
            "%lu,PPG,%lu,%lu,%lu,0,0,0,0,0,0,0\r\n",
            rec->timestamp_ms, rec->seq, rec->data.ppg.ir, rec->data.ppg.red);

    case SENSOR_REC_IMU:
        return snprintf(buf, len,
            "%lu,IMU,%lu,%d,%d,%d,%d,%d,%d,0,0,0\r\n",
            rec->timestamp_ms, rec->seq,
            rec->data.imu.ax, rec->data.imu.ay, rec->data.imu.az,
            rec->data.imu.gx, rec->data.imu.gy, rec->data.imu.gz);

    default:
        return snprintf(buf, len, "%lu,UNK,%lu,0,0,0,0,0,0,0,0,0\r\n",
            rec->timestamp_ms, rec->seq);
    }
}

void StartTask_SDWriter(void *argument)
{
    (void)argument;

    FRESULT res;
    UINT bw = 0;
    SensorRecord_t rec;
    char line[SD_WRITE_LINE_BUF_SIZE];

    osDelay(4000);
    SDLogger_InitQueue();

    Safe_USB_Printf("[SDLOG] Task started\r\n");

    if (Mtx_SDCardHandle != NULL) {
        osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    }

    res = f_mount(&SDFatFS, SDPath, 1);
    if (res != FR_OK) {
        Safe_USB_Printf("[SDLOG][ERR] f_mount failed: %d\r\n", res);
        if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
        for (;;) osDelay(1000);
    }

    res = f_open(&s_log_file, SD_LOG_FILE_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        Safe_USB_Printf("[SDLOG][ERR] f_open failed: %d\r\n", res);
        f_mount(NULL, SDPath, 1);
        if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
        for (;;) osDelay(1000);
    }

    const char *header = "timestamp_ms,type,seq,v1,v2,v3,v4,v5,v6,v7,v8,extra\r\n";
    f_write(&s_log_file, header, strlen(header), &bw);
    f_sync(&s_log_file);

    if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);

    Safe_USB_Printf("[SDLOG] File opened: %s\r\n", SD_LOG_FILE_PATH);

    for (;;) {
        if (osMessageQueueGet(Q_SensorRecordHandle, &rec, NULL, osWaitForever) == osOK) {
            int n = SDLogger_RecordToCsvLine(&rec, line, sizeof(line));
            if (n <= 0) continue;

            if (Mtx_SDCardHandle != NULL) osMutexAcquire(Mtx_SDCardHandle, osWaitForever);

            bw = 0;
            res = f_write(&s_log_file, line, (UINT)n, &bw);

            if (res == FR_OK && bw == (UINT)n) {
                s_written_records++;
            } else {
                Safe_USB_Printf("[SDLOG][ERR] f_write failed: res=%d bw=%lu n=%d\r\n",
                                res, (uint32_t)bw, n);
            }

            if ((s_written_records % SD_SYNC_EVERY_RECORDS) == 0) {
                f_sync(&s_log_file);
                Safe_USB_Printf("[SDLOG] synced records=%lu dropped=%lu\r\n",
                                s_written_records, s_dropped_records);
            }

            if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
        }
    }
}

/* ================================================================
 * PPG INT 诊断专用队列 + Writer — 写入 0:/ppg_int_diag.csv
 * ================================================================ */
#define PPG_DIAG_QUEUE_DEPTH       256
#define PPG_DIAG_FILE_PATH         "0:/ppg_int_diag.csv"
#define PPG_DIAG_SYNC_EVERY        128

static osMessageQueueId_t Q_PPGDiagHandle = NULL;
static FIL s_ppg_diag_file;
static uint32_t s_ppg_diag_written = 0;
static uint32_t s_ppg_diag_dropped = 0;

void PPGDiag_InitQueue(void)
{
    if (Q_PPGDiagHandle == NULL) {
        Q_PPGDiagHandle = osMessageQueueNew(
            PPG_DIAG_QUEUE_DEPTH,
            sizeof(SensorRecord_t),
            NULL);
    }
}

void PPGDiag_Enqueue(const SensorRecord_t *rec)
{
    if (rec == NULL || Q_PPGDiagHandle == NULL) return;

    if (osMessageQueuePut(Q_PPGDiagHandle, rec, 0, 0) != osOK) {
        s_ppg_diag_dropped++;
    }
}

static int PPGDiag_RecordToCsvLine(const SensorRecord_t *rec, char *buf, size_t len)
{
    if (rec == NULL || buf == NULL || len == 0) return 0;

    const uint8_t s1 = rec->data.ppg_int_diag.status1;
    return snprintf(buf, len,
        "%lu,%lu,%lu,%u,%02X,%u,%u,%u,%u,%u,%02X,%u,%u,%u\r\n",
        (unsigned long)rec->timestamp_ms,
        (unsigned long)rec->seq,
        (unsigned long)rec->data.ppg_int_diag.irq_count,
        (unsigned)rec->data.ppg_int_diag.pin_before,
        s1,
        (s1 & 0x80) ? 1 : 0,
        (s1 & 0x40) ? 1 : 0,
        (s1 & 0x20) ? 1 : 0,
        (s1 & 0x01) ? 1 : 0,
        (unsigned)rec->data.ppg_int_diag.pin_after,
        rec->data.ppg_int_diag.ie1,
        (unsigned)rec->data.ppg_int_diag.fifo_wr,
        (unsigned)rec->data.ppg_int_diag.fifo_rd,
        (unsigned)rec->data.ppg_int_diag.fifo_ov);
}

void StartTask_PPGDiagWriter(void *argument)
{
    (void)argument;
    FRESULT res;
    UINT bw;
    SensorRecord_t rec;
    char line[192];

    osDelay(500);

    if (Mtx_SDCardHandle != NULL)
        osMutexAcquire(Mtx_SDCardHandle, osWaitForever);

    res = f_mount(&SDFatFS, SDPath, 1);
    if (res != FR_OK) {
        if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
        for (;;) osDelay(1000);
    }

    res = f_open(&s_ppg_diag_file, PPG_DIAG_FILE_PATH,
                 FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        f_mount(NULL, SDPath, 1);
        if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
        for (;;) osDelay(1000);
    }

    {
        const char *hdr =
            "timestamp_ms,seq,irq_count,pin_before,status1_hex,"
            "a_full,ppg_rdy,alc_ovf,pwr_rdy,pin_after,"
            "ie1_hex,fifo_wr,fifo_rd,fifo_ov\r\n";
        f_write(&s_ppg_diag_file, hdr, strlen(hdr), &bw);
        f_sync(&s_ppg_diag_file);
    }

    if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);

    for (;;) {
        if (osMessageQueueGet(Q_PPGDiagHandle, &rec, NULL,
                              osWaitForever) == osOK) {
            int n = PPGDiag_RecordToCsvLine(&rec, line, sizeof(line));
            if (n <= 0) continue;

            if (Mtx_SDCardHandle != NULL)
                osMutexAcquire(Mtx_SDCardHandle, osWaitForever);

            bw = 0;
            res = f_write(&s_ppg_diag_file, line, (UINT)n, &bw);

            if (res == FR_OK && bw == (UINT)n) {
                s_ppg_diag_written++;
            }

            if ((s_ppg_diag_written % PPG_DIAG_SYNC_EVERY) == 0)
                f_sync(&s_ppg_diag_file);

            if (Mtx_SDCardHandle != NULL)
                osMutexRelease(Mtx_SDCardHandle);
        }
    }
}