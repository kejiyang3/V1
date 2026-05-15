#include "multi_sensor_logger.h"
#include "ecg_record_control.h"
#include "sd_debug_log.h"
#include "fatfs.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

extern osMutexId_t Mtx_SDCardHandle;

/* ========== 队列 ========== */
#define MS_QUEUE_DEPTH      12
osMessageQueueId_t Q_MultiSensorBlockHandle = NULL;

void MultiSensorLogger_InitQueue(void)
{
    if (Q_MultiSensorBlockHandle == NULL) {
        Q_MultiSensorBlockHandle = osMessageQueueNew(
            MS_QUEUE_DEPTH, sizeof(MS_BlockMsg_t), NULL);
    }
}

/* ========== 双缓冲 ========== */
static ECG_Block_t s_ecg_blocks[2];
static PPG_Block_t s_ppg_blocks[2];
static IMU_Block_t s_imu_blocks[2];

static uint8_t s_ecg_active = 0;
static uint8_t s_ppg_active = 0;
static uint8_t s_imu_active = 0;

static volatile uint8_t s_ecg_block_free[2] = {1, 1};
static volatile uint8_t s_ppg_block_free[2] = {1, 1};
static volatile uint8_t s_imu_block_free[2] = {1, 1};

static uint32_t s_ecg_seq = 0;
static uint32_t s_ppg_seq = 0;
static uint32_t s_imu_seq = 0;

/* 统计 */
static volatile uint32_t s_ppg_sample_count = 0;
static volatile uint32_t s_imu_sample_count = 0;
static volatile uint32_t s_ecg_block_drop = 0;
static volatile uint32_t s_ppg_block_drop = 0;
static volatile uint32_t s_imu_block_drop = 0;

/* 真实写入成功/失败计数 */
static volatile uint32_t s_ecg_write_ok  = 0;
static volatile uint32_t s_ecg_write_fail = 0;
static volatile uint32_t s_ppg_write_ok  = 0;
static volatile uint32_t s_ppg_write_fail = 0;
static volatile uint32_t s_imu_write_ok  = 0;
static volatile uint32_t s_imu_write_fail = 0;

/* stop 请求标志 */
static volatile uint8_t s_stop_requested = 0;

/* 文件状态 */
static FIL s_ms_file;
static volatile uint8_t s_file_opened = 0;

/* ========== 内部：提交 block ========== */
static void submit_ecg_block(uint16_t count)
{
    uint8_t idx = s_ecg_active;
    MS_BlockMsg_t msg;
    msg.type = MS_BLOCK_ECG;
    msg.block_index = idx;
    msg.count = count;

    if (osMessageQueuePut(Q_MultiSensorBlockHandle, &msg, 0, 0) == osOK) {
        uint8_t next = idx ^ 1;
        if (s_ecg_block_free[next]) {
            s_ecg_active = next;
            s_ecg_block_free[idx] = 0;
        } else {
            s_ecg_block_drop++;
            s_ecg_blocks[idx].count = 0;
        }
    } else {
        s_ecg_block_drop++;
        s_ecg_blocks[idx].count = 0;
    }
}

static void submit_ppg_block(uint16_t count)
{
    uint8_t idx = s_ppg_active;
    MS_BlockMsg_t msg;
    msg.type = MS_BLOCK_PPG;
    msg.block_index = idx;
    msg.count = count;

    if (osMessageQueuePut(Q_MultiSensorBlockHandle, &msg, 0, 0) == osOK) {
        uint8_t next = idx ^ 1;
        if (s_ppg_block_free[next]) {
            s_ppg_active = next;
            s_ppg_block_free[idx] = 0;
        } else {
            s_ppg_block_drop++;
            s_ppg_blocks[idx].count = 0;
        }
    } else {
        s_ppg_block_drop++;
        s_ppg_blocks[idx].count = 0;
    }
}

static void submit_imu_block(uint16_t count)
{
    uint8_t idx = s_imu_active;
    MS_BlockMsg_t msg;
    msg.type = MS_BLOCK_IMU;
    msg.block_index = idx;
    msg.count = count;

    if (osMessageQueuePut(Q_MultiSensorBlockHandle, &msg, 0, 0) == osOK) {
        uint8_t next = idx ^ 1;
        if (s_imu_block_free[next]) {
            s_imu_active = next;
            s_imu_block_free[idx] = 0;
        } else {
            s_imu_block_drop++;
            s_imu_blocks[idx].count = 0;
        }
    } else {
        s_imu_block_drop++;
        s_imu_blocks[idx].count = 0;
    }
}

/* ========== Add Sample ========== */

void MultiSensorLogger_AddECG(int16_t ecg)
{
    if (g_ecg_rec.state != ECG_REC_RECORDING) return;

    uint8_t idx = s_ecg_active;
    ECG_Block_t *blk = &s_ecg_blocks[idx];

    uint16_t pos = blk->count;
    blk->timestamp_ms[pos] = HAL_GetTick();
    blk->seq[pos] = s_ecg_seq++;
    blk->ecg[pos] = ecg;
    blk->count = pos + 1;

    g_ecg_rec.ecg_sample_count++;

    if (blk->count >= ECG_BLOCK_SAMPLES) {
        submit_ecg_block(blk->count);
    }
}

void MultiSensorLogger_AddPPG(uint32_t ir, uint32_t red)
{
    if (g_ecg_rec.state != ECG_REC_RECORDING) return;

    uint8_t idx = s_ppg_active;
    PPG_Block_t *blk = &s_ppg_blocks[idx];

    uint16_t pos = blk->count;
    blk->timestamp_ms[pos] = HAL_GetTick();
    blk->seq[pos] = s_ppg_seq++;
    blk->ir[pos] = ir;
    blk->red[pos] = red;
    blk->count = pos + 1;

    s_ppg_sample_count++;

    if (blk->count >= PPG_BLOCK_SAMPLES) {
        submit_ppg_block(blk->count);
    }
}

void MultiSensorLogger_AddIMU(int16_t ax, int16_t ay, int16_t az,
                              int16_t gx, int16_t gy, int16_t gz)
{
    if (g_ecg_rec.state != ECG_REC_RECORDING) return;

    uint8_t idx = s_imu_active;
    IMU_Block_t *blk = &s_imu_blocks[idx];

    uint16_t pos = blk->count;
    blk->timestamp_ms[pos] = HAL_GetTick();
    blk->seq[pos] = s_imu_seq++;
    blk->ax[pos] = ax;
    blk->ay[pos] = ay;
    blk->az[pos] = az;
    blk->gx[pos] = gx;
    blk->gy[pos] = gy;
    blk->gz[pos] = gz;
    blk->count = pos + 1;

    s_imu_sample_count++;

    if (blk->count >= IMU_BLOCK_SAMPLES) {
        submit_imu_block(blk->count);
    }
}

/* ========== Stop 时 flush 半满 block ========== */

void MultiSensorLogger_RequestStopAndFlush(void)
{
    s_stop_requested = 1;

    SD_DebugLog_WriteLine("MULTI_SENSOR_FLUSH_BEGIN");

    if (s_ecg_blocks[s_ecg_active].count > 0) {
        submit_ecg_block(s_ecg_blocks[s_ecg_active].count);
    }
    if (s_ppg_blocks[s_ppg_active].count > 0) {
        submit_ppg_block(s_ppg_blocks[s_ppg_active].count);
    }
    if (s_imu_blocks[s_imu_active].count > 0) {
        submit_imu_block(s_imu_blocks[s_imu_active].count);
    }

    SD_DebugLog_WriteLine("MULTI_SENSOR_FLUSH_END");
}

void MultiSensorLogger_ResetForNewRecording(void)
{
    s_stop_requested = 0;
    s_ecg_seq = 0;
    s_ppg_seq = 0;
    s_imu_seq = 0;

    s_ppg_sample_count = 0;
    s_imu_sample_count = 0;
    s_ecg_block_drop = 0;
    s_ppg_block_drop = 0;
    s_imu_block_drop = 0;

    s_ecg_write_ok   = 0;
    s_ecg_write_fail = 0;
    s_ppg_write_ok   = 0;
    s_ppg_write_fail = 0;
    s_imu_write_ok   = 0;
    s_imu_write_fail = 0;

    for (int i = 0; i < 2; i++) {
        s_ecg_blocks[i].count = 0;
        s_ppg_blocks[i].count = 0;
        s_imu_blocks[i].count = 0;
        s_ecg_block_free[i] = 1;
        s_ppg_block_free[i] = 1;
        s_imu_block_free[i] = 1;
    }
    s_ecg_active = 0;
    s_ppg_active = 0;
    s_imu_active = 0;

    g_ecg_rec.ecg_sample_count = 0;
    g_ecg_rec.ecg_written_count = 0;
    g_ecg_rec.ecg_drop_count = 0;
    g_ecg_rec.sd_write_bytes = 0;
    g_ecg_rec.sd_sync_count = 0;
    g_ecg_rec.start_tick = HAL_GetTick();
}

uint8_t MultiSensorLogger_IsFileOpened(void)
{
    return s_file_opened;
}

/* ========== Writer: block 释放 ========== */
static void free_ecg_block(uint8_t idx) { s_ecg_blocks[idx].count = 0; s_ecg_block_free[idx] = 1; }
static void free_ppg_block(uint8_t idx) { s_ppg_blocks[idx].count = 0; s_ppg_block_free[idx] = 1; }
static void free_imu_block(uint8_t idx) { s_imu_blocks[idx].count = 0; s_imu_block_free[idx] = 1; }

/* ========== checked f_write helper ========== */
static inline int sd_write_checked(FIL *fp, const char *line, UINT len,
                                   uint32_t *ok, uint32_t *fail,
                                   uint32_t *bytes_out)
{
    UINT bw = 0;
    FRESULT res = f_write(fp, line, len, &bw);
    if (res == FR_OK && bw == len) {
        (*ok)++;
        *bytes_out += bw;
        return 1;
    }
    (*fail)++;
    return 0;
}

/* ========== Writer: CSV 写入函数 ========== */

static void write_ecg_block(FIL *fp, uint8_t idx)
{
    ECG_Block_t *blk = &s_ecg_blocks[idx];
    char line[64];
    UINT bw = 0;

    for (uint16_t i = 0; i < blk->count; i++) {
        int n = snprintf(line, sizeof(line),
            "%lu,ECG,%lu,%d,0,0,0,0,0\r\n",
            blk->timestamp_ms[i], blk->seq[i], blk->ecg[i]);
        if (n > 0 && n < (int)sizeof(line)) {
            sd_write_checked(fp, line, (UINT)n,
                             &s_ecg_write_ok, &s_ecg_write_fail,
                             &g_ecg_rec.sd_write_bytes);
            g_ecg_rec.ecg_written_count++;
        }
    }
    free_ecg_block(idx);
}

static void write_ppg_block(FIL *fp, uint8_t idx)
{
    PPG_Block_t *blk = &s_ppg_blocks[idx];
    char line[64];
    UINT bw = 0;

    for (uint16_t i = 0; i < blk->count; i++) {
        int n = snprintf(line, sizeof(line),
            "%lu,PPG,%lu,%lu,%lu,0,0,0,0\r\n",
            blk->timestamp_ms[i], blk->seq[i], blk->ir[i], blk->red[i]);
        if (n > 0 && n < (int)sizeof(line)) {
            sd_write_checked(fp, line, (UINT)n,
                             &s_ppg_write_ok, &s_ppg_write_fail,
                             &g_ecg_rec.sd_write_bytes);
        }
    }
    free_ppg_block(idx);
}

static void write_imu_block(FIL *fp, uint8_t idx)
{
    IMU_Block_t *blk = &s_imu_blocks[idx];
    char line[72];
    UINT bw = 0;

    for (uint16_t i = 0; i < blk->count; i++) {
        int n = snprintf(line, sizeof(line),
            "%lu,IMU,%lu,%d,%d,%d,%d,%d,%d\r\n",
            blk->timestamp_ms[i], blk->seq[i],
            blk->ax[i], blk->ay[i], blk->az[i],
            blk->gx[i], blk->gy[i], blk->gz[i]);
        if (n > 0 && n < (int)sizeof(line)) {
            sd_write_checked(fp, line, (UINT)n,
                             &s_imu_write_ok, &s_imu_write_fail,
                             &g_ecg_rec.sd_write_bytes);
        }
    }
    free_imu_block(idx);
}

/* ========== Writer 任务 ========== */

#define MS_SYNC_EVERY_BLOCKS    8

void StartTask_MultiSensor_SDWriter(void *argument)
{
    (void)argument;
    MS_BlockMsg_t msg;
    FRESULT res;
    UINT bw;
    uint32_t total_blocks = 0;

    MultiSensorLogger_InitQueue();

    for (;;) {
        /* 等待 RECORDING 状态 */
        while (g_ecg_rec.state != ECG_REC_RECORDING) {
            osDelay(50);
        }

        s_stop_requested = 0;
        s_file_opened = 0;
        total_blocks = 0;

        /* 打开文件 */
        if (Mtx_SDCardHandle != NULL) {
            if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(2000)) != osOK) {
                g_ecg_rec.state = ECG_REC_ERROR;
                continue;
            }
        }

        res = f_mount(&SDFatFS, SDPath, 1);
        if (res != FR_OK) {
            SD_DebugLog_WriteLine("MS_WRITER_MOUNT_FAIL");
            if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
            g_ecg_rec.state = ECG_REC_ERROR;
            continue;
        }

        res = f_open(&s_ms_file, g_ecg_rec.file_name, FA_CREATE_ALWAYS | FA_WRITE);
        if (res != FR_OK) {
            SD_DebugLog_WriteLine("MS_WRITER_OPEN_FAIL");
            if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
            g_ecg_rec.state = ECG_REC_ERROR;
            continue;
        }

        /* 写表头 (检查结果) */
        {
            const char *header = "timestamp_ms,type,seq,v1,v2,v3,v4,v5,v6\r\n";
            UINT hdr_len = (UINT)strlen(header);
            UINT hdr_bw = 0;
            res = f_write(&s_ms_file, header, hdr_len, &hdr_bw);
            if (res != FR_OK || hdr_bw != hdr_len) {
                SD_DebugLog_WriteLine("MS_WRITER_HEADER_FAIL");
                f_close(&s_ms_file);
                if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
                g_ecg_rec.state = ECG_REC_ERROR;
                continue;
            }
            g_ecg_rec.sd_write_bytes += hdr_bw;
        }
        f_sync(&s_ms_file);
        g_ecg_rec.sd_sync_count++;

        s_file_opened = 1;
        g_ecg_rec.sd_file_opened = 1;
        g_ecg_rec.sd_file_closed = 0;

        if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);

        SD_DebugLog_WriteLine("MULTI_SENSOR_FILE_OPENED");

        /* 主循环：取 block 写入 */
        while (g_ecg_rec.state == ECG_REC_RECORDING ||
               g_ecg_rec.state == ECG_REC_STOPPING ||
               osMessageQueueGetCount(Q_MultiSensorBlockHandle) > 0) {

            if (osMessageQueueGet(Q_MultiSensorBlockHandle, &msg, NULL,
                                  pdMS_TO_TICKS(50)) == osOK) {
                if (Mtx_SDCardHandle != NULL) {
                    osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
                }

                switch (msg.type) {
                case MS_BLOCK_ECG:
                    write_ecg_block(&s_ms_file, msg.block_index);
                    break;
                case MS_BLOCK_PPG:
                    write_ppg_block(&s_ms_file, msg.block_index);
                    break;
                case MS_BLOCK_IMU:
                    write_imu_block(&s_ms_file, msg.block_index);
                    break;
                default:
                    break;
                }
                total_blocks++;

                if ((total_blocks % MS_SYNC_EVERY_BLOCKS) == 0) {
                    f_sync(&s_ms_file);
                    g_ecg_rec.sd_sync_count++;
                }

                if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
            }

            if (s_stop_requested &&
                osMessageQueueGetCount(Q_MultiSensorBlockHandle) == 0) {
                break;
            }
        }

        /* 关闭文件 — 仅 fsync+close，不 unmount */
        if (Mtx_SDCardHandle != NULL) {
            osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
        }

        f_sync(&s_ms_file);
        f_close(&s_ms_file);
        /* 不调用 f_mount(NULL)，避免影响 PPGDiagWriter 等持有文件的任务 */

        s_file_opened = 0;
        g_ecg_rec.sd_file_closed = 1;
        g_ecg_rec.stop_tick = HAL_GetTick();
        g_ecg_rec.state = ECG_REC_STOPPED;

        if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);

        /* 写统计摘要到 debug_log */
        {
            char stats[256];
            int n = snprintf(stats, sizeof(stats),
                "MULTI_STATS,"
                "ecg_samples=%lu,ecg_write_ok=%lu,ecg_write_fail=%lu,"
                "ppg_samples=%lu,ppg_write_ok=%lu,ppg_write_fail=%lu,"
                "imu_samples=%lu,imu_write_ok=%lu,imu_write_fail=%lu,"
                "ecg_drop_blk=%lu,ppg_drop_blk=%lu,imu_drop_blk=%lu,"
                "sd_bytes=%lu,sync_count=%lu",
                (unsigned long)g_ecg_rec.ecg_written_count,
                (unsigned long)s_ecg_write_ok,
                (unsigned long)s_ecg_write_fail,
                (unsigned long)s_ppg_sample_count,
                (unsigned long)s_ppg_write_ok,
                (unsigned long)s_ppg_write_fail,
                (unsigned long)s_imu_sample_count,
                (unsigned long)s_imu_write_ok,
                (unsigned long)s_imu_write_fail,
                (unsigned long)s_ecg_block_drop,
                (unsigned long)s_ppg_block_drop,
                (unsigned long)s_imu_block_drop,
                (unsigned long)g_ecg_rec.sd_write_bytes,
                (unsigned long)g_ecg_rec.sd_sync_count);
            if (n > 0 && n < (int)sizeof(stats)) {
                SD_DebugLog_WriteLine(stats);
            }
        }
        SD_DebugLog_WriteLine("MULTI_SENSOR_RECORD_STOPPED");
    }
}
