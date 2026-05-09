#include "max3003.h"
#include "spi.h"
#include "app_log.h"
#include "ecg_record_control.h"
#include "sd_debug_log.h"
#include <stdio.h>
#include <string.h>

__attribute__((weak)) void Packagedata_AddEcgSample(int16_t ecg)
{
    (void)ecg;
}

void MAX30003_CS_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    HAL_GPIO_WritePin(ECG_CS_GPIO_Port, ECG_CS_Pin, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = ECG_CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ECG_CS_GPIO_Port, &GPIO_InitStruct);
}

static int MAX30003_WriteVerify(uint8_t reg, uint32_t expected, const char *name)
{
    uint32_t readback = 0;
    if (MAX30003_WriteReg(reg, expected) != HAL_OK) {
        APP_USB_LOG("[MAX30003][ERR] WRITE %s failed\r\n", name);
        return 0;
    }
    HAL_Delay(1);
    if (MAX30003_ReadReg(reg, &readback) != HAL_OK) {
        APP_USB_LOG("[MAX30003][ERR] READBACK %s failed\r\n", name);
        return 0;
    }
    if (readback != expected) {
        APP_USB_LOG("[MAX30003][ERR] %s mismatch: wrote=0x%06lX read=0x%06lX\r\n",
                   name, expected, readback);
        return 0;
    }
    APP_USB_LOG("[MAX30003][OK] %s = 0x%06lX\r\n", name, readback);
    return 1;
}

HAL_StatusTypeDef MAX30003_WriteReg(uint8_t reg, uint32_t data)
{
    uint8_t tx[4];
    tx[0] = (reg << 1) | 0x00;
    tx[1] = (uint8_t)((data >> 16) & 0xFF);
    tx[2] = (uint8_t)((data >> 8)  & 0xFF);
    tx[3] = (uint8_t)( data        & 0xFF);
    ECG_CSB_LOW();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi3, tx, 4, 100);
    ECG_CSB_HIGH();
    return st;
}

HAL_StatusTypeDef MAX30003_ReadReg(uint8_t reg, uint32_t *data)
{
    uint8_t tx[4] = {0};
    uint8_t rx[4] = {0};
    tx[0] = (reg << 1) | 0x01;
    ECG_CSB_LOW();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi3, tx, rx, 4, 100);
    ECG_CSB_HIGH();
    if (st == HAL_OK && data != NULL) {
        *data = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | (uint32_t)rx[3];
    }
    return st;
}

void MAX30003_SwReset(void)
{
    MAX30003_WriteReg(MAX30003_SW_RST, 0x000000);
    HAL_Delay(10);
}

void MAX30003_Synch(void)
{
    MAX30003_WriteReg(MAX30003_SYNCH, 0x000000);
}

void MAX30003_FifoReset(void)
{
    MAX30003_WriteReg(MAX30003_FIFO_RST, 0x000000);
}

void MAX30003_Init(void)
{
    uint32_t dummy = 0;
    uint32_t info1 = 0;

    APP_USB_LOG("[MAX30003] Initializing...\r\n");

    MAX30003_CS_Init();
    MAX30003_SwReset();
    HAL_Delay(20);

    MAX30003_ReadReg(MAX30003_STATUS, &dummy);
    HAL_Delay(2);
    MAX30003_ReadReg(MAX30003_STATUS, &dummy);

    if (MAX30003_ReadReg(MAX30003_INFO, &info1) != HAL_OK) return;
    APP_USB_LOG("[MAX30003] INFO=0x%06lX\r\n", info1);

    /* CNFG_GEN */
    if (!MAX30003_WriteVerify(MAX30003_CNFG_GEN,
                              MAX30003_CNFG_GEN_NORMAL,
                              "CNFG_GEN")) return;

    /* CNFG_CAL: 外部输入模式, 禁用内部校准 */
    if (!MAX30003_WriteVerify(MAX30003_CNFG_CAL,
                              MAX30003_CNFG_CAL_EXTERNAL,
                              "CNFG_CAL_EXTERNAL")) return;

    /* CNFG_EMUX: 外部输入模式, ECGP/ECGN 直通 */
    if (!MAX30003_WriteVerify(MAX30003_CNFG_EMUX,
                              MAX30003_CNFG_EMUX_EXTERNAL,
                              "CNFG_EMUX_EXTERNAL")) return;

    /* CNFG_ECG: 512SPS, 20x, 0.5Hz HPF, 100Hz LPF */
    if (!MAX30003_WriteVerify(MAX30003_CNFG_ECG,
                              MAX30003_CNFG_ECG_NORMAL,
                              "CNFG_ECG")) return;

    /* 等待 PLL 锁定 */
    uint8_t retry = 50;
    while (retry--) {
        MAX30003_ReadReg(MAX30003_STATUS, &dummy);
        if ((dummy & MAX30003_STATUS_PLLINT) == 0) break;
        HAL_Delay(2);
    }

    /* EN_INT: IDLE (先不开中断) */
    if (!MAX30003_WriteVerify(MAX30003_EN_INT,
                              MAX30003_EN_INT_IDLE,
                              "EN_INT")) return;

    /* MNGR_INT: EFIT=4, 约5样本触发 EINT */
    if (!MAX30003_WriteVerify(MAX30003_MNGR_INT,
                              MAX30003_MNGR_INT_FAST,
                              "MNGR_INT")) return;

    MAX30003_FifoReset();
    MAX30003_Synch();
    MAX30003_ReadReg(MAX30003_STATUS, &dummy);

    APP_USB_LOG("[MAX30003] Init done. STATUS=0x%06lX\r\n", dummy);

    MAX30003_SaveRegisterSnapshotToDebugLog("AFTER_INIT");
}

void MAX30003_StartStream(void)
{
    uint32_t status1 = 0, status2 = 0, status3 = 0;

    /* 先关中断, 避免清 FIFO/SYNCH 时触发 */
    (void)MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);

    /* Start 前重新清 FIFO, 清除 Init→Start 间旧数据 */
    MAX30003_FifoReset();
    MAX30003_Synch();

    /* 读 STATUS 清旧 sticky flags */
    (void)MAX30003_ReadReg(MAX30003_STATUS, &status1);
    (void)MAX30003_ReadReg(MAX30003_STATUS, &status2);

    /* 开 EINT + EOVF, 不含 PLLINT */
    if (MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_NORMAL) != HAL_OK) {
        return;
    }

    (void)MAX30003_ReadReg(MAX30003_STATUS, &status3);
    g_ecg_rec.last_status = status3;

    MAX30003_SaveRegisterSnapshotToDebugLog("AFTER_START");
}

void MAX30003_StopStream(void)
{
    uint32_t status = 0;

    (void)MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);
    MAX30003_FifoReset();
    MAX30003_Synch();

    (void)MAX30003_ReadReg(MAX30003_STATUS, &status);
    g_ecg_rec.last_status = status;

    MAX30003_SaveRegisterSnapshotToDebugLog("AFTER_STOP");
}

int16_t MAX30003_ConvertData(uint32_t raw_data)
{
    int32_t val;
    val = (raw_data >> 6) & 0x3FFFF;
    if (val & 0x20000) val |= 0xFFFC0000;
    val >>= 2;
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    return (int16_t)val;
}

static HAL_StatusTypeDef MAX30003_ReadFifoBurst(uint32_t *samples, uint8_t max_samples)
{
    if (max_samples == 0 || max_samples > FIFO_BURST_SIZE) return HAL_ERROR;
    uint16_t total = 1 + max_samples * 3;
    uint8_t tx[1 + FIFO_BURST_SIZE * 3] = {0};
    uint8_t rx[1 + FIFO_BURST_SIZE * 3] = {0};
    tx[0] = (MAX30003_ECG_FIFO_BURST << 1) | 0x01;
    ECG_CSB_LOW();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi3, tx, rx, total, 200);
    ECG_CSB_HIGH();
    if (st != HAL_OK) return st;
    for (int i = 0; i < max_samples; i++) {
        int off = 1 + i * 3;
        samples[i] = ((uint32_t)rx[off] << 16) | ((uint32_t)rx[off+1] << 8) | rx[off+2];
    }
    return HAL_OK;
}

static void MAX30003_UpdateStatusStats(uint32_t status_reg)
{
    g_ecg_rec.last_status = status_reg;

    if (status_reg & MAX30003_STATUS_PLLINT) {
        g_ecg_rec.pll_status_seen_count++;
        if (!g_ecg_rec.pll_current_set) {
            g_ecg_rec.pll_edge_count++;
            g_ecg_rec.pll_current_set = 1;
        }
    } else {
        g_ecg_rec.pll_current_set = 0;
    }
}

void MAX30003_Task(void)
{
    for (uint8_t drain = 0; drain < 4; drain++) {
        uint32_t status_reg = 0;

        if (MAX30003_ReadReg(MAX30003_STATUS, &status_reg) != HAL_OK) return;
        MAX30003_UpdateStatusStats(status_reg);

        if (status_reg & MAX30003_STATUS_EOVF) {
            g_ecg_rec.fifo_eovf_count++;
            MAX30003_FifoReset();
            MAX30003_Synch();
            uint32_t dummy = 0;
            MAX30003_ReadReg(MAX30003_STATUS, &dummy);
            g_ecg_rec.last_status = dummy;
            return;
        }

        if ((status_reg & MAX30003_STATUS_EINT) == 0) return;

        uint32_t samples[FIFO_BURST_SIZE];
        if (MAX30003_ReadFifoBurst(samples, FIFO_BURST_SIZE) != HAL_OK) return;

        for (uint8_t i = 0; i < FIFO_BURST_SIZE; i++) {
            uint32_t raw_data = samples[i];
            uint8_t etag = (raw_data >> 3) & 0x07;

            if (etag == 0x00 || etag == 0x02) {
                int16_t ecg_val = MAX30003_ConvertData(raw_data);
                g_ecg_rec.fifo_sample_count++;
                Packagedata_AddEcgSample(ecg_val);
                if (etag == 0x02) break;
            } else if (etag == 0x01 || etag == 0x03) {
                if (etag == 0x03) break;
            } else if (etag == 0x06) {
                g_ecg_rec.fifo_empty_count++;
                break;
            } else if (etag == 0x07) {
                g_ecg_rec.fifo_etag_overflow_count++;
                g_ecg_rec.fifo_eovf_count++;
                MAX30003_FifoReset();
                MAX30003_Synch();
                return;
            } else {
                break;
            }
        }
    }
}

uint8_t MAX30003_CheckExternalInputConfig(void)
{
    uint32_t cal = 0, emux = 0;

    if (MAX30003_ReadReg(MAX30003_CNFG_CAL, &cal) != HAL_OK) return 0;
    if (MAX30003_ReadReg(MAX30003_CNFG_EMUX, &emux) != HAL_OK) return 0;

    if (cal != 0x000000UL) return 0;
    if (emux != 0x000000UL) return 0;

    return 1;
}

void MAX30003_SaveRegisterSnapshotToDebugLog(const char *tag)
{
    uint32_t info = 0, status = 0;
    uint32_t gen = 0, cal = 0, emux = 0, ecg = 0, enint = 0, mngr = 0;

    MAX30003_ReadReg(MAX30003_INFO, &info);
    MAX30003_ReadReg(MAX30003_STATUS, &status);
    MAX30003_ReadReg(MAX30003_CNFG_GEN, &gen);
    MAX30003_ReadReg(MAX30003_CNFG_CAL, &cal);
    MAX30003_ReadReg(MAX30003_CNFG_EMUX, &emux);
    MAX30003_ReadReg(MAX30003_CNFG_ECG, &ecg);
    MAX30003_ReadReg(MAX30003_EN_INT, &enint);
    MAX30003_ReadReg(MAX30003_MNGR_INT, &mngr);

    SD_DebugLog_WriteRegisterSnapshot(tag, info, status,
                                       gen, cal, emux,
                                       ecg, enint, mngr);
}

void MAX30003_Diagnostic_Dump(void)
{
    /* 保留空, 当前不使用 USB 诊断 */
}
