#include "ecg_usb_dump.h"
#include "ecg_record_control.h"
#include "fatfs.h"
#include "ff.h"
#include "usbd_cdc_if.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>

extern void Safe_USB_Printf(const char *format, ...);
extern osMutexId_t Mtx_SDCardHandle;

static volatile uint8_t s_usb_dump_request = 0;

#define ECG_USB_DUMP_BUF_SIZE   256

/* 调试阶段限制发送字节数，0 表示完整文件 */
#define ECG_USB_DUMP_MAX_BYTES  0

void ECG_USB_RequestDump(void)
{
    s_usb_dump_request = 1;
}

/**
  * @brief  从 SD 卡读取 ecg_v1.csv，通过 USB CDC 阻塞发送
  */
static void ECG_USB_DumpFile_Blocking(void)
{
    FIL file;
    FRESULT res;
    UINT br = 0;
    uint8_t buf[ECG_USB_DUMP_BUF_SIZE];
    uint32_t total_sent = 0;
    uint8_t partial = 0;

    Safe_USB_Printf("[USB_DUMP] state=%d sd_file_closed=%d file=%s\r\n",
                    g_ecg_rec.state, g_ecg_rec.sd_file_closed, g_ecg_rec.file_name);

    /* 检查状态 — 正在录制时不允许 dump */
    if (g_ecg_rec.state == ECG_REC_RECORDING ||
        g_ecg_rec.state == ECG_REC_STOPPING) {
        Safe_USB_Printf("[USB_DUMP][WARN] ECG is still recording. Stop first.\r\n");
        return;
    }

    Safe_USB_Printf("[USB_DUMP] begin dump file: %s\r\n", g_ecg_rec.file_name);

    /* SD 卡互斥锁 */
    if (Mtx_SDCardHandle != NULL) {
        osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    }

    res = f_mount(&SDFatFS, SDPath, 1);
    if (res != FR_OK) {
        Safe_USB_Printf("[USB_DUMP][ERR] f_mount failed: %d\r\n", res);
        goto release;
    }

    res = f_open(&file, g_ecg_rec.file_name, FA_READ);
    if (res != FR_OK) {
        Safe_USB_Printf("[USB_DUMP][ERR] f_open failed: %d\r\n", res);
        goto release;
    }

    /* 发送文件头标记 */
    CDC_Transmit_FS_Blocking((uint8_t*)"\r\n[ECG_FILE_BEGIN]\r\n", 19, 3000);

    /* 分块读取并发送 */
    while (1) {
        br = 0;
        res = f_read(&file, buf, sizeof(buf), &br);

        if (res != FR_OK) {
            Safe_USB_Printf("[USB_DUMP][ERR] f_read failed: %d\r\n", res);
            break;
        }

        if (br == 0) {
            break;  /* 文件结束 */
        }

        /* 限制发送长度 */
        if (ECG_USB_DUMP_MAX_BYTES != 0 &&
            total_sent + br > ECG_USB_DUMP_MAX_BYTES) {
            uint16_t remain = ECG_USB_DUMP_MAX_BYTES - total_sent;
            if (remain > 0) {
                uint8_t ret = CDC_Transmit_FS_Blocking(buf, remain, 3000);
                if (ret != USBD_OK) {
                    Safe_USB_Printf("[USB_DUMP][ERR] CDC send failed: %u\r\n", ret);
                } else {
                    total_sent += remain;
                }
            }
            partial = 1;
            break;
        }

        uint8_t ret = CDC_Transmit_FS_Blocking(buf, (uint16_t)br, 3000);
        if (ret != USBD_OK) {
            Safe_USB_Printf("[USB_DUMP][ERR] CDC send failed: %u\r\n", ret);
            break;
        }

        total_sent += br;
    }

    /* 发送文件尾标记 */
    CDC_Transmit_FS_Blocking((uint8_t*)"\r\n[ECG_FILE_END]\r\n", 19, 3000);

    f_close(&file);
    f_mount(NULL, SDPath, 1);

release:
    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    if (partial) {
        Safe_USB_Printf("[USB_DUMP] partial dump finished, sent_bytes=%lu (max %lu)\r\n",
                        total_sent, (uint32_t)ECG_USB_DUMP_MAX_BYTES);
    } else {
        Safe_USB_Printf("[USB_DUMP] finished, sent_bytes=%lu\r\n", total_sent);
    }
}

void StartTask_ECG_USBDump(void *argument)
{
    (void)argument;

    Safe_USB_Printf("[USB_DUMP] task entered\r\n");

    for (;;) {
        if (s_usb_dump_request) {
            s_usb_dump_request = 0;
            Safe_USB_Printf("[USB_DUMP] request received\r\n");

            /* SD 文件未关闭时等待 */
            if (!g_ecg_rec.sd_file_closed) {
                Safe_USB_Printf("[USB_DUMP][WARN] SD file not closed, waiting\r\n");
                uint32_t tw = HAL_GetTick();
                while (!g_ecg_rec.sd_file_closed && (HAL_GetTick() - tw < 3000)) {
                    osDelay(50);
                }
            }

            ECG_USB_DumpFile_Blocking();
        }

        osDelay(100);
    }
}
