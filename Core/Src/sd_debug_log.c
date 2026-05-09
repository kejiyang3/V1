#include "sd_debug_log.h"
#include "fatfs.h"
#include "ff.h"
#include "cmsis_os.h"
#include "main.h"
#include "ecg_record_control.h"
#include <stdio.h>
#include <string.h>

extern osMutexId_t Mtx_SDCardHandle;

#define SD_DEBUG_LOG_PATH "0:/debug_log.txt"

static FRESULT SD_DebugLog_AppendRaw(const char *text)
{
    FIL file;
    FRESULT res;
    UINT bw = 0;

    if (text == NULL) return FR_INT_ERR;

    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, 500) != osOK) return FR_INT_ERR;
    }

    res = f_mount(&SDFatFS, SDPath, 1);
    if (res == FR_OK) {
        res = f_open(&file, SD_DEBUG_LOG_PATH, FA_OPEN_APPEND | FA_WRITE);
        if (res == FR_OK) {
            f_write(&file, text, strlen(text), &bw);
            f_sync(&file);
            f_close(&file);
        }
    }

    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    return res;
}

void SD_DebugLog_Init(void)
{
    char line[128];
    snprintf(line, sizeof(line), "\r\nBOOT tick=%lu\r\n", HAL_GetTick());
    SD_DebugLog_AppendRaw(line);
}

void SD_DebugLog_WriteLine(const char *line)
{
    char buf[192];
    if (line == NULL) return;
    snprintf(buf, sizeof(buf), "%lu,%s\r\n", HAL_GetTick(), line);
    SD_DebugLog_AppendRaw(buf);
}

void SD_DebugLog_WriteEvent(const char *tag, uint32_t value)
{
    char buf[192];
    if (tag == NULL) return;
    snprintf(buf, sizeof(buf), "%lu,%s,%lu\r\n", HAL_GetTick(), tag, value);
    SD_DebugLog_AppendRaw(buf);
}

void SD_DebugLog_WriteSnapshot(void)
{
    char buf[384];
    snprintf(buf, sizeof(buf),
             "%lu,SNAPSHOT,state=%d,samples=%lu,written=%lu,drop=%lu,bytes=%lu,sync=%lu,"
             "eovf=%lu,pll_seen=%lu,pll_edge=%lu,status=0x%06lX\r\n",
             HAL_GetTick(),
             g_ecg_rec.state,
             g_ecg_rec.ecg_sample_count,
             g_ecg_rec.ecg_written_count,
             g_ecg_rec.ecg_drop_count,
             g_ecg_rec.sd_write_bytes,
             g_ecg_rec.sd_sync_count,
             g_ecg_rec.fifo_eovf_count,
             g_ecg_rec.pll_status_seen_count,
             g_ecg_rec.pll_edge_count,
             g_ecg_rec.last_status);
    SD_DebugLog_AppendRaw(buf);
}
