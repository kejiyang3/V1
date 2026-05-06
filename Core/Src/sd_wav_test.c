/**
 * @file    sd_wav_test.c
 * @brief   SD card WAV file creation test task
 * @note    Creates an empty 44-byte WAV header file on SD card via FatFS.
 *          Runs once, then stays alive with osDelay().
 */

#include "sd_wav_test.h"

#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include "ff.h"
#include "usb_printf.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define WAV_FILE_PATH          "0:/test.wav"
#define WAV_SAMPLE_RATE        16000U
#define WAV_BITS_PER_SAMPLE    16U
#define WAV_CHANNELS           1U

extern osMutexId_t Mtx_SDCardHandle;

/* FatFs structs moved to static to avoid stack overflow (FIL contains 512-byte sector buffer) */
static FIL sd_wav_file;
static FILINFO sd_wav_fno;

static void WAV_MakeHeader(uint8_t header[44],
                           uint32_t sample_rate,
                           uint16_t bits_per_sample,
                           uint16_t channels,
                           uint32_t data_size)
{
    uint32_t byte_rate   = sample_rate * channels * bits_per_sample / 8U;
    uint16_t block_align = (uint16_t)(channels * bits_per_sample / 8U);
    uint32_t riff_size   = 36U + data_size;

    memset(header, 0, 44);

    /* RIFF chunk descriptor */
    memcpy(&header[0], "RIFF", 4);
    header[4] = (uint8_t)(riff_size & 0xFF);
    header[5] = (uint8_t)((riff_size >> 8) & 0xFF);
    header[6] = (uint8_t)((riff_size >> 16) & 0xFF);
    header[7] = (uint8_t)((riff_size >> 24) & 0xFF);

    memcpy(&header[8], "WAVE", 4);

    /* fmt sub-chunk */
    memcpy(&header[12], "fmt ", 4);
    header[16] = 16;     /* PCM fmt chunk size = 16 */
    header[17] = 0;
    header[18] = 0;
    header[19] = 0;

    header[20] = 1;      /* Audio format = 1 (PCM) */
    header[21] = 0;

    header[22] = (uint8_t)(channels & 0xFF);
    header[23] = (uint8_t)((channels >> 8) & 0xFF);

    header[24] = (uint8_t)(sample_rate & 0xFF);
    header[25] = (uint8_t)((sample_rate >> 8) & 0xFF);
    header[26] = (uint8_t)((sample_rate >> 16) & 0xFF);
    header[27] = (uint8_t)((sample_rate >> 24) & 0xFF);

    header[28] = (uint8_t)(byte_rate & 0xFF);
    header[29] = (uint8_t)((byte_rate >> 8) & 0xFF);
    header[30] = (uint8_t)((byte_rate >> 16) & 0xFF);
    header[31] = (uint8_t)((byte_rate >> 24) & 0xFF);

    header[32] = (uint8_t)(block_align & 0xFF);
    header[33] = (uint8_t)((block_align >> 8) & 0xFF);

    header[34] = (uint8_t)(bits_per_sample & 0xFF);
    header[35] = (uint8_t)((bits_per_sample >> 8) & 0xFF);

    /* data sub-chunk */
    memcpy(&header[36], "data", 4);
    header[40] = (uint8_t)(data_size & 0xFF);
    header[41] = (uint8_t)((data_size >> 8) & 0xFF);
    header[42] = (uint8_t)((data_size >> 16) & 0xFF);
    header[43] = (uint8_t)((data_size >> 24) & 0xFF);
}

static const char *FR_ToString(FRESULT res)
{
    switch (res) {
    case FR_OK:               return "FR_OK";
    case FR_DISK_ERR:         return "FR_DISK_ERR";
    case FR_INT_ERR:          return "FR_INT_ERR";
    case FR_NOT_READY:        return "FR_NOT_READY";
    case FR_NO_FILE:          return "FR_NO_FILE";
    case FR_NO_PATH:          return "FR_NO_PATH";
    case FR_INVALID_NAME:     return "FR_INVALID_NAME";
    case FR_DENIED:           return "FR_DENIED";
    case FR_EXIST:            return "FR_EXIST";
    case FR_INVALID_OBJECT:   return "FR_INVALID_OBJECT";
    case FR_WRITE_PROTECTED:  return "FR_WRITE_PROTECTED";
    case FR_INVALID_DRIVE:    return "FR_INVALID_DRIVE";
    case FR_NOT_ENABLED:      return "FR_NOT_ENABLED";
    case FR_NO_FILESYSTEM:    return "FR_NO_FILESYSTEM";
    case FR_MKFS_ABORTED:     return "FR_MKFS_ABORTED";
    case FR_TIMEOUT:          return "FR_TIMEOUT";
    case FR_LOCKED:           return "FR_LOCKED";
    case FR_NOT_ENOUGH_CORE:  return "FR_NOT_ENOUGH_CORE";
    case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
    case FR_INVALID_PARAMETER:   return "FR_INVALID_PARAMETER";
    default:                  return "FR_UNKNOWN";
    }
}

void StartTask_SDWavTest(void *argument)
{
    FRESULT res;
    UINT bw = 0;
    static uint8_t wav_header[44];
    FIL *file = &sd_wav_file;
    FILINFO *info = &sd_wav_fno;

    /* Wait for USB CDC enumeration and PC to open serial port */
    osDelay(3000);

    usb_printf("\r\n========== SD WAV TEST START ==========\r\n");

    if (Mtx_SDCardHandle != NULL) {
        osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    }

    usb_printf("[SD] Mounting... path=%s\r\n", SDPath);

    usb_printf("[SD] Before f_mount\r\n");
    res = f_mount(&SDFatFS, SDPath, 1);
    usb_printf("[SD] After f_mount, res=%d\r\n", (int)res);
    if (res != FR_OK) {
        usb_printf("[SD] f_mount failed: %s (%d)\r\n", FR_ToString(res), res);
        goto exit_unlock;
    }

    usb_printf("[SD] Mount OK\r\n");

    usb_printf("[SD] Before f_open\r\n");
    res = f_open(file, WAV_FILE_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    usb_printf("[SD] After f_open, res=%d\r\n", (int)res);
    if (res != FR_OK) {
        usb_printf("[SD] f_open failed: %s (%d), file=%s\r\n",
                   FR_ToString(res), res, WAV_FILE_PATH);
        goto unmount;
    }

    usb_printf("[SD] File created: %s\r\n", WAV_FILE_PATH);

    WAV_MakeHeader(wav_header,
                   WAV_SAMPLE_RATE,
                   WAV_BITS_PER_SAMPLE,
                   WAV_CHANNELS,
                   0U);

    usb_printf("[SD] Before f_write\r\n");
    res = f_write(file, wav_header, sizeof(wav_header), &bw);
    usb_printf("[SD] After f_write, res=%d, bw=%lu\r\n", (int)res, (uint32_t)bw);
    if (res != FR_OK || bw != sizeof(wav_header)) {
        usb_printf("[SD] f_write failed: %s (%d), bw=%lu\r\n",
                   FR_ToString(res), res, (uint32_t)bw);
        f_close(file);
        goto unmount;
    }

    usb_printf("[SD] WAV header written: %lu bytes\r\n", (uint32_t)bw);

    usb_printf("[SD] Before f_sync\r\n");
    res = f_sync(file);
    usb_printf("[SD] After f_sync, res=%d\r\n", (int)res);
    if (res != FR_OK) {
        usb_printf("[SD] f_sync failed: %s (%d)\r\n", FR_ToString(res), res);
        f_close(file);
        goto unmount;
    }

    usb_printf("[SD] Before f_close\r\n");
    res = f_close(file);
    usb_printf("[SD] After f_close, res=%d\r\n", (int)res);
    if (res != FR_OK) {
        usb_printf("[SD] f_close failed: %s (%d)\r\n", FR_ToString(res), res);
        goto unmount;
    }

    usb_printf("[SD] File closed\r\n");

    usb_printf("[SD] Before f_stat\r\n");
    res = f_stat(WAV_FILE_PATH, info);
    usb_printf("[SD] After f_stat, res=%d\r\n", (int)res);
    if (res != FR_OK) {
        usb_printf("[SD] f_stat failed: %s (%d)\r\n", FR_ToString(res), res);
        goto unmount;
    }

    usb_printf("[SD] File info:\r\n");
    usb_printf("     name = %s\r\n", info->fname);
    usb_printf("     size = %lu bytes\r\n", (uint32_t)info->fsize);
    usb_printf("     attr = 0x%02X\r\n", info->fattrib);
    usb_printf("[SD] Test OK\r\n");

unmount:
    res = f_mount(NULL, SDPath, 1);
    if (res == FR_OK) {
        usb_printf("[SD] Unmount OK\r\n");
    } else {
        usb_printf("[SD] Unmount failed: %s (%d)\r\n", FR_ToString(res), res);
    }

exit_unlock:
    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    usb_printf("=========== SD WAV TEST END ===========\r\n");

    /* Run once, then stay alive.
     * Do NOT suspend or delete this task during debugging.
     * Otherwise debugger may stop in FreeRTOS Idle cleanup path.
     */
    for (;;) {
        osDelay(1000);
    }
}
