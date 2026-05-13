#include "max3003.h"
#include "spi.h"
#include "usart.h"
#include "usb_printf.h"
#include "app_log.h"
#include "ecg_record_control.h"
#include "sd_debug_log.h"
#include <stdio.h>
#include <string.h>

/* 外部引用 — Packagedata_AddEcgSample 弱实现 (可被外部覆盖) */
__attribute__((weak)) void Packagedata_AddEcgSample(int16_t ecg)
{
    (void)ecg;
    /* 默认空实现, 由外部模块 (如 edf_storage.c) 覆盖 */
}

/* DC Lead-Off 状态缓存 */
static MAX30003_LeadStatus_t g_lead_status = {
    .state = MAX30003_LEAD_UNKNOWN,
    .p_off = 0,
    .n_off = 0,
    .dc_loff = 0,
    .raw_status = 0,
    .last_update_ms = 0
};

/**
  * @brief  CS 引脚初始化
  */
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

/**
  * @brief  写寄存器并读回验证
  */
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

/**
  * @brief  SPI 写寄存器
  */
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

    if(st != HAL_OK) {
        APP_USB_LOG("[SPI_ERR] WriteReg failed! HAL_Status: %d, SPI_State: %d, ErrorCode: %lu\r\n",
                   st, HAL_SPI_GetState(&hspi3), HAL_SPI_GetError(&hspi3));
    }
    return st;
}

/**
  * @brief  SPI 读寄存器
  */
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
    } else {
        APP_USB_LOG("[SPI_ERR] ReadReg failed! HAL_Status: %d, SPI_State: %d, ErrorCode: %lu\r\n",
                   st, HAL_SPI_GetState(&hspi3), HAL_SPI_GetError(&hspi3));
    }
    return st;
}

/**
  * @brief  软件复位
  */
void MAX30003_SwReset(void)
{
    MAX30003_WriteReg(MAX30003_SW_RST, 0x000000);
    HAL_Delay(10);
}

/**
  * @brief  同步时序
  */
void MAX30003_Synch(void)
{
    MAX30003_WriteReg(MAX30003_SYNCH, 0x000000);
}

/**
  * @brief  FIFO 复位
  */
void MAX30003_FifoReset(void)
{
    MAX30003_WriteReg(MAX30003_FIFO_RST, 0x000000);
}

/**
  * @brief  初始化 MAX30003 (仅配置寄存器, 不开启 EINT/EOVF)
  */
void MAX30003_Init(void)
{
    uint32_t dummy = 0;
    uint32_t info1 = 0, info2 = 0, info3 = 0;

    APP_USB_LOG("[MAX30003] Initializing...\r\n");

    MAX30003_CS_Init();

    MAX30003_SwReset();
    HAL_Delay(20);

    /* 清掉复位后的旧 STATUS */
    MAX30003_ReadReg(MAX30003_STATUS, &dummy);
    HAL_Delay(2);
    MAX30003_ReadReg(MAX30003_STATUS, &dummy);

    if (MAX30003_ReadReg(MAX30003_INFO, &info1) != HAL_OK) return;
    HAL_Delay(1);
    if (MAX30003_ReadReg(MAX30003_INFO, &info2) != HAL_OK) return;
    HAL_Delay(1);
    if (MAX30003_ReadReg(MAX30003_INFO, &info3) != HAL_OK) return;

    APP_USB_LOG("[MAX30003] INFO=0x%06lX\r\n", info1);

    /* CNFG_ECG: 先配好采样率/增益/滤波，再开 CNFG_GEN */
    if (!MAX30003_WriteVerify(MAX30003_CNFG_ECG,
                              MAX30003_CNFG_ECG_NORMAL,
                              "CNFG_ECG")) return;

    /* CNFG_GEN: 一次性写入 EN_ECG + EN_RBIAS + DCLOFF (0x081217) */
    if (!MAX30003_WriteVerify(MAX30003_CNFG_GEN,
                              MAX30003_CNFG_GEN_NORMAL,
                              "CNFG_GEN")) return;

#if MAX30003_USE_INTERNAL_CAL_TEST
    if (!MAX30003_WriteVerify(MAX30003_CNFG_CAL,
                              MAX30003_CNFG_CAL_1HZ_BIPOLAR,
                              "CNFG_CAL")) return;

    if (!MAX30003_WriteVerify(MAX30003_CNFG_EMUX,
                              MAX30003_CNFG_EMUX_CAL_DIFF,
                              "CNFG_EMUX")) return;
#else
    if (!MAX30003_WriteVerify(MAX30003_CNFG_CAL,
                              0x000000,
                              "CNFG_CAL")) return;

    if (!MAX30003_WriteVerify(MAX30003_CNFG_EMUX,
                              0x000000,
                              "CNFG_EMUX")) return;
#endif

    /* Auto Fast Recovery: 双电极运动场景快速恢复 */
    if (!MAX30003_WriteVerify(MAX30003_MNGR_DYN,
                              MAX30003_MNGR_DYN_AUTO_FAST,
                              "MNGR_DYN")) return;

    /* 等待 PLL 锁定 */
    uint8_t retry = 50;
    while(retry--) {
        MAX30003_ReadReg(MAX30003_STATUS, &dummy);
        if((dummy & MAX30003_STATUS_PLLINT) == 0) break;
        HAL_Delay(2);
    }

    if (!MAX30003_WriteVerify(MAX30003_EN_INT,
                              MAX30003_EN_INT_IDLE,
                              "EN_INT")) return;

    /* EFIT=4，约 5 个样本触发一次中断 */
    if (!MAX30003_WriteVerify(MAX30003_MNGR_INT,
                              MAX30003_MNGR_INT_FAST,
                              "MNGR_INT")) return;

    /* 清 FIFO 并同步 */
    MAX30003_FifoReset();
    MAX30003_Synch();

    MAX30003_ReadReg(MAX30003_STATUS, &dummy);
    APP_USB_LOG("[MAX30003] Init done. STATUS=0x%06lX\r\n", dummy);

    /* 一次性读回关键寄存器到 SD debug_log */
    {
        uint32_t gen = 0, emux = 0, ecg = 0, status = 0;
        MAX30003_ReadReg(MAX30003_CNFG_GEN, &gen);
        MAX30003_ReadReg(MAX30003_CNFG_EMUX, &emux);
        MAX30003_ReadReg(MAX30003_CNFG_ECG, &ecg);
        MAX30003_ReadReg(MAX30003_STATUS, &status);
        SD_DebugLog_WriteEvent("INIT_GEN", gen);
        SD_DebugLog_WriteEvent("INIT_EMUX", emux);
        SD_DebugLog_WriteEvent("INIT_ECG", ecg);
        SD_DebugLog_WriteEvent("INIT_STATUS", status);
    }
}

/**
  * @brief  启动 ECG 采集流 — 开始前重置 FIFO/SYNCH/STATUS
  */
void MAX30003_StartStream(void)
{
    uint32_t status1 = 0;
    uint32_t status2 = 0;
    uint32_t status3 = 0;

    /* 先关闭正常 ECG 中断，避免清 FIFO/SYNCH 期间触发任务通知 */
    (void)MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);

    /* 关键: 真正开始记录前重新 FIFO_RST + SYNCH，清除 Init→Start 之间的旧数据 */
    MAX30003_FifoReset();
    MAX30003_Synch();

    /* 连续读两次 STATUS 清掉旧的 sticky flags */
    (void)MAX30003_ReadReg(MAX30003_STATUS, &status1);
    (void)MAX30003_ReadReg(MAX30003_STATUS, &status2);

    /* 打开正常 ECG 中断 */
    if (MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_NORMAL) != HAL_OK) {
        APP_USB_LOG("[MAX30003][ERR] StartStream write EN_INT_NORMAL failed\r\n");
        return;
    }

    (void)MAX30003_ReadReg(MAX30003_STATUS, &status3);

    g_ecg_rec.last_status = status3;
    MAX30003_UpdateLeadStatus(status3);

    APP_USB_LOG("[MAX30003] StartStream status1=0x%06lX status2=0x%06lX status3=0x%06lX\r\n",
                status1, status2, status3);
}

/**
  * @brief  停止 ECG 采集流 — 关闭中断并清 FIFO
  */
void MAX30003_StopStream(void)
{
    uint32_t status = 0;

    (void)MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);

    /* 清 FIFO + SYNCH，避免下一次 Start 带入旧样本 */
    MAX30003_FifoReset();
    MAX30003_Synch();

    (void)MAX30003_ReadReg(MAX30003_STATUS, &status);
    g_ecg_rec.last_status = status;

    APP_USB_LOG("[MAX30003] StopStream done. STATUS=0x%06lX\r\n", status);
}

/**
  * @brief  将 18位 ECG 数据转换为 int16_t
  */
int16_t MAX30003_ConvertData(uint32_t raw_data)
{
    int32_t val;

    // 提取 18 位 ECG 数据 (D[23:6])
    val = (raw_data >> 6) & 0x3FFFF;

    // 符号扩展 (18-bit signed to 32-bit)
    if (val & 0x20000)
    {
        val |= 0xFFFC0000;
    }

    // 右移 2 位以适应 int16_t 范围 (18-bit -> 16-bit)
    // 注意: MAX30003 的 ENOB 是 15.5 位，所以丢弃最低 2 位刚好能完美装入 int16_t，同时滤除多余底噪。
    val >>= 2;

    // 饱和处理，防止溢出
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;

    return (int16_t)val;
}

/**
  * @brief  Burst 模式读取 FIFO
  */
static HAL_StatusTypeDef MAX30003_ReadFifoBurst(uint32_t *samples, uint8_t max_samples)
{
    if (max_samples == 0 || max_samples > FIFO_BURST_SIZE) return HAL_ERROR;

    // 1字节命令 + 每样本3字节
    uint16_t total = 1 + max_samples * 3; 
    uint8_t tx[1 + FIFO_BURST_SIZE * 3] = {0};
    uint8_t rx[1 + FIFO_BURST_SIZE * 3] = {0};
    
    tx[0] = (MAX30003_ECG_FIFO_BURST << 1) | 0x01;

    ECG_CSB_LOW();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi3, tx, rx, total, 200);
    ECG_CSB_HIGH();

    if (st != HAL_OK) return st;

    for (int i = 0; i < max_samples; i++) {
        int off = 1 + i * 3; // 精简了索引偏移逻辑
        samples[i] = ((uint32_t)rx[off] << 16) | ((uint32_t)rx[off+1] << 8) | rx[off+2];
    }

    return HAL_OK;
}

/**
  * @brief  更新 STATUS 相关统计 (PLL seen / edge / last_status)
  */
static void MAX30003_UpdateStatusStats(uint32_t status_reg)
{
    g_ecg_rec.last_status = status_reg;

    if (status_reg & MAX30003_STATUS_PLLINT) {
        g_ecg_rec.pll_status_seen_count++;
        g_ecg_rec.pll_warn_count = g_ecg_rec.pll_status_seen_count;

        /* 边沿检测: 只有从 0→1 才加 edge count */
        if (!g_ecg_rec.pll_current_set) {
            g_ecg_rec.pll_edge_count++;
            g_ecg_rec.pll_current_set = 1;
        }
    } else {
        g_ecg_rec.pll_current_set = 0;
    }
}

/**
  * @brief  提取并处理 MAX30003 FIFO 数据 (drain loop, 最多 4 轮)
  * @note   Burst 读 FIFO，一次 32 word；避免在采样路径里调用阻塞/打印函数。
  */
void MAX30003_Task(void)
{
    uint8_t drain;

    for (drain = 0; drain < 4; drain++) {
        uint32_t status_reg = 0;

        if (MAX30003_ReadReg(MAX30003_STATUS, &status_reg) != HAL_OK) {
            return;
        }

        MAX30003_UpdateStatusStats(status_reg);
        MAX30003_UpdateLeadStatus(status_reg);

        /* 处理 FIFO overflow */
        if (status_reg & MAX30003_STATUS_EOVF) {
            g_ecg_rec.fifo_eovf_count++;
            MAX30003_FifoReset();
            MAX30003_Synch();

            uint32_t dummy = 0;
            MAX30003_ReadReg(MAX30003_STATUS, &dummy);
            g_ecg_rec.last_status = dummy;
            return;
        }

        /* 无 EINT 则退出 drain */
        if ((status_reg & MAX30003_STATUS_EINT) == 0) {
            return;
        }

        uint32_t samples[FIFO_BURST_SIZE];

        if (MAX30003_ReadFifoBurst(samples, FIFO_BURST_SIZE) != HAL_OK) {
            return;
        }

        for (uint8_t i = 0; i < FIFO_BURST_SIZE; i++) {
            uint32_t raw_data = samples[i];
            uint8_t etag = (raw_data >> 3) & 0x07;

            if (etag == 0x00 || etag == 0x02) {
                int16_t ecg_val = MAX30003_ConvertData(raw_data);

                g_ecg_rec.fifo_sample_count++;
                Packagedata_AddEcgSample(ecg_val);

                if (etag == 0x02) break;
            }
            else if (etag == 0x01 || etag == 0x03) {
                if (etag == 0x03) break;
            }
            else if (etag == 0x06) {
                g_ecg_rec.fifo_empty_count++;
                break;
            }
            else if (etag == 0x07) {
                g_ecg_rec.fifo_etag_overflow_count++;
                g_ecg_rec.fifo_eovf_count++;
                MAX30003_FifoReset();
                MAX30003_Synch();
                return;
            }
            else {
                break;
            }
        }
    }
}

int32_t MAX30003_Read_Sample(void)
{
    uint32_t raw_data;
    if (MAX30003_ReadReg(MAX30003_ECG_FIFO, &raw_data) != HAL_OK) {
        return 0;
    }
    return (int32_t)raw_data;
}

void MAX30003_UpdateLeadStatus(uint32_t status)
{
    uint8_t ph = (status & MAX30003_STATUS_LDOFF_PH) ? 1 : 0;
    uint8_t pl = (status & MAX30003_STATUS_LDOFF_PL) ? 1 : 0;
    uint8_t nh = (status & MAX30003_STATUS_LDOFF_NH) ? 1 : 0;
    uint8_t nl = (status & MAX30003_STATUS_LDOFF_NL) ? 1 : 0;

    g_lead_status.raw_status = status;
    g_lead_status.last_update_ms = HAL_GetTick();
    g_lead_status.dc_loff = (status & MAX30003_STATUS_DCLOFFINT) ? 1 : 0;
    g_lead_status.p_off = (ph || pl) ? 1 : 0;
    g_lead_status.n_off = (nh || nl) ? 1 : 0;

    if (g_lead_status.dc_loff || ph || pl || nh || nl) {
        g_lead_status.state = MAX30003_LEAD_OFF;
    } else {
        g_lead_status.state = MAX30003_LEAD_ON;
    }
}

void MAX30003_GetLeadStatus(MAX30003_LeadStatus_t *out)
{
    if (out == NULL) return;
    *out = g_lead_status;
}

void MAX30003_PollLeadStatus(void)
{
    uint32_t status = 0;
    if (MAX30003_ReadReg(MAX30003_STATUS, &status) == HAL_OK) {
        MAX30003_UpdateStatusStats(status);
        MAX30003_UpdateLeadStatus(status);
    }
}

void MAX30003_Diagnostic_Dump(void)
{
    uint32_t reg[5];

    APP_USB_LOG("\r\n======================================================\r\n");
    APP_USB_LOG("         MAX30003 COMPREHENSIVE DIAGNOSTIC            \r\n");
    APP_USB_LOG("======================================================\r\n");

    /* Read core configuration registers */
    MAX30003_ReadReg(MAX30003_INFO, &reg[0]);
    MAX30003_ReadReg(MAX30003_CNFG_GEN, &reg[1]);
    MAX30003_ReadReg(MAX30003_CNFG_EMUX, &reg[2]);
    MAX30003_ReadReg(MAX30003_CNFG_ECG, &reg[3]);
    MAX30003_ReadReg(MAX30003_CNFG_CAL, &reg[4]);

    APP_USB_LOG("[0x0F] INFO       : 0x%06X\r\n", (unsigned int)reg[0]);
    APP_USB_LOG("[0x10] CNFG_GEN   : 0x%06X\r\n", (unsigned int)reg[1]);
    APP_USB_LOG("[0x14] CNFG_EMUX  : 0x%06X (OPENP=%lu, OPENN=%lu)\r\n",
               (unsigned int)reg[2], (reg[2]>>21)&1, (reg[2]>>20)&1);
    APP_USB_LOG("[0x15] CNFG_ECG   : 0x%06X\r\n", (unsigned int)reg[3]);
    APP_USB_LOG("[0x12] CNFG_CAL   : 0x%06X\r\n", (unsigned int)reg[4]);

    APP_USB_LOG("\r\n--- Executing Active DC Lead-Off Test ---\r\n");
    APP_USB_LOG("Checking physical PCB trace continuity...\r\n");

    /*
     * Enable DC Lead-Off for testing:
     * EN_DCLOFF=01 (bits 13:12), IMAG=010 for 10nA (bits 10:8)
     */
    MAX30003_WriteReg(MAX30003_CNFG_GEN, MAX30003_CNFG_GEN_NORMAL | (1UL << 12) | (2UL << 8));

    /* Wait 200ms for internal comparators to stabilize (Datasheet requires > 115ms) */
    HAL_Delay(200);

    uint32_t status_reg;
    MAX30003_ReadReg(MAX30003_STATUS, &status_reg);
    APP_USB_LOG("[0x01] STATUS     : 0x%06X\r\n", (unsigned int)status_reg);

    uint8_t loff = status_reg & 0x0F;
    APP_USB_LOG("\r\n[HARDWARE DIAGNOSIS RESULT]:\r\n");
    if (loff == 0) {
        APP_USB_LOG(" -> CLOSED: Both electrodes are physically connected.\r\n");
    } else {
        if (loff & 0x08) APP_USB_LOG(" -> [FAIL] ECGP (Positive / Pin 6) is physically OPEN or floating!\r\n");
        if (loff & 0x04) APP_USB_LOG(" -> [FAIL] ECGP (Positive / Pin 6) is SHORTED to GND!\r\n");
        if (loff & 0x02) APP_USB_LOG(" -> [FAIL] ECGN (Negative / Pin 7) is physically OPEN or floating!\r\n");
        if (loff & 0x01) APP_USB_LOG(" -> [FAIL] ECGN (Negative / Pin 7) is SHORTED to GND!\r\n");
    }

    /* Restore normal clean AFE configuration */
    MAX30003_WriteReg(MAX30003_CNFG_GEN, MAX30003_CNFG_GEN_NORMAL);

    APP_USB_LOG("======================================================\r\n");
}