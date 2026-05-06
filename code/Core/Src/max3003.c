#include "max3003.h"
#include "spi.h"
#include "usart.h"
#include "usb_printf.h"
#include <stdio.h>
#include <string.h>

/* 外部引用 — RTOS-safe USB 打印 (定义在 freertos.c) */
extern void Safe_USB_Printf(const char *format, ...);

/* 外部引用 — Packagedata_AddEcgSample 弱实现 (可被外部覆盖) */
__attribute__((weak)) void Packagedata_AddEcgSample(int16_t ecg)
{
    (void)ecg;
    /* 默认空实现, 由外部模块 (如 edf_storage.c) 覆盖 */
}

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
        Safe_USB_Printf("[MAX30003][ERR] WRITE %s failed\r\n", name);
        return 0;
    }

    HAL_Delay(1);

    if (MAX30003_ReadReg(reg, &readback) != HAL_OK) {
        Safe_USB_Printf("[MAX30003][ERR] READBACK %s failed\r\n", name);
        return 0;
    }

    if (readback != expected) {
        Safe_USB_Printf("[MAX30003][ERR] %s mismatch: wrote=0x%06lX read=0x%06lX\r\n",
                   name, expected, readback);
        return 0;
    }

    Safe_USB_Printf("[MAX30003][OK] %s = 0x%06lX\r\n", name, readback);
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
        Safe_USB_Printf("[SPI_ERR] WriteReg failed! HAL_Status: %d, SPI_State: %d, ErrorCode: %lu\r\n",
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
        Safe_USB_Printf("[SPI_ERR] ReadReg failed! HAL_Status: %d, SPI_State: %d, ErrorCode: %lu\r\n",
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

    Safe_USB_Printf("[MAX30003] Initializing...\r\n");

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

    Safe_USB_Printf("[MAX30003] INFO readback: 0x%06lX / 0x%06lX / 0x%06lX\r\n",
               info1, info2, info3);

    if (!MAX30003_WriteVerify(MAX30003_CNFG_GEN,
                              MAX30003_CNFG_GEN_NORMAL,
                              "CNFG_GEN")) return;

    if (!MAX30003_WriteVerify(MAX30003_CNFG_CAL,
                              0x000000,
                              "CNFG_CAL")) return;

    if (!MAX30003_WriteVerify(MAX30003_CNFG_EMUX,
                              0x000000,
                              "CNFG_EMUX")) return;

    if (!MAX30003_WriteVerify(MAX30003_CNFG_ECG,
                              MAX30003_CNFG_ECG_NORMAL,
                              "CNFG_ECG")) return;

    /* 等待 PLL 锁定 (数据手册强烈建议在 SYNCH 之前确保 PLL 锁定) */
    uint8_t retry = 50;
    while(retry--) {
        MAX30003_ReadReg(MAX30003_STATUS, &dummy);
        if((dummy & MAX30003_STATUS_PLLINT) == 0) break;
        HAL_Delay(2);
    }

    if (!MAX30003_WriteVerify(MAX30003_EN_INT,
                              MAX30003_EN_INT_IDLE,
                              "EN_INT_IDLE")) return;

    /* EFIT=4，约 5 个样本触发一次中断 */
    if (!MAX30003_WriteVerify(MAX30003_MNGR_INT,
                              MAX30003_MNGR_INT_FAST,
                              "MNGR_INT")) return;

    /* 清 FIFO 并同步，无需 Delay，防止 FIFO 重新堆积 */
    Safe_USB_Printf("[DBG] before FIFO_RST\r\n");
    MAX30003_FifoReset();

    Safe_USB_Printf("[DBG] after FIFO_RST, before SYNCH\r\n");
    MAX30003_Synch();

    Safe_USB_Printf("[DBG] after SYNCH, reading STATUS\r\n");
    if (MAX30003_ReadReg(MAX30003_STATUS, &dummy) != HAL_OK) {
        Safe_USB_Printf("[DBG][ERR] Read STATUS failed after SYNCH\r\n");
        return;
    }

    Safe_USB_Printf("[DBG] STATUS after init = 0x%06lX\r\n", dummy);
    Safe_USB_Printf("[MAX30003] Init done. Waiting for MAX30003_StartStream().\r\n");
}

/**
  * @brief  启动 ECG 采集流 (FIFO_RST + SYNCH + 开启 EINT/EOVF)
  */
void MAX30003_StartStream(void)
{
    uint32_t dummy = 0;

    Safe_USB_Printf("[DBG] StartStream begin\r\n");

    /* 启动前先关闭 EINT/EOVF，只保留 INTB Open-Drain 类型 */
    MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);
    Safe_USB_Printf("[DBG] EN_INT_IDLE written\r\n");

    /* 重新开始一段干净的 ECG 记录 */
    MAX30003_FifoReset();
    Safe_USB_Printf("[DBG] FIFO reset in StartStream\r\n");

    MAX30003_Synch();
    Safe_USB_Printf("[DBG] SYNCH in StartStream\r\n");

    /* 清掉旧 STATUS 锁存位 */
    if (MAX30003_ReadReg(MAX30003_STATUS, &dummy) == HAL_OK) {
        Safe_USB_Printf("[DBG] STATUS before enable INT = 0x%06lX\r\n", dummy);
    } else {
        Safe_USB_Printf("[DBG][ERR] STATUS read failed before enable INT\r\n");
    }
    HAL_Delay(1);
    if (MAX30003_ReadReg(MAX30003_STATUS, &dummy) == HAL_OK) {
        Safe_USB_Printf("[DBG] STATUS second read = 0x%06lX\r\n", dummy);
    } else {
        Safe_USB_Printf("[DBG][ERR] STATUS second read failed\r\n");
    }

    /* 任务准备就绪，再打开 EINT/EOVF */
    MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_NORMAL);
    Safe_USB_Printf("[DBG] EN_INT_NORMAL written\r\n");

    if (MAX30003_ReadReg(MAX30003_STATUS, &dummy) == HAL_OK) {
        Safe_USB_Printf("[DBG] STATUS after enable INT = 0x%06lX\r\n", dummy);
    } else {
        Safe_USB_Printf("[DBG][ERR] STATUS failed after enable INT\r\n");
    }

    Safe_USB_Printf("[MAX30003] Stream started. FIFO reset + INT enabled.\r\n");
}

/**
  * @brief  停止 ECG 采集流
  */
void MAX30003_StopStream(void)
{
    uint32_t dummy = 0;

    MAX30003_WriteReg(MAX30003_EN_INT, MAX30003_EN_INT_IDLE);
    MAX30003_FifoReset();
    MAX30003_ReadReg(MAX30003_STATUS, &dummy);

    Safe_USB_Printf("[MAX30003] Stream stopped.\r\n");
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
  * @brief  提取并处理 MAX30003 FIFO 数据 (由 EXTI 下降沿唤醒后调用)
  * @note   Burst 读 FIFO，一次 32 word；避免在采样路径里调用阻塞/打印函数。
  */
void MAX30003_Task(void)
{
    uint32_t status_reg = 0;
    static uint32_t poll_cnt = 0;
    static uint32_t ovf_count = 0;
    static uint32_t pll_warn_count = 0;
    static uint32_t sample_count = 0;
    extern volatile uint32_t ecg_irq_count;

    if (MAX30003_ReadReg(MAX30003_STATUS, &status_reg) != HAL_OK) {
        return;
    }

    poll_cnt++;

    /* 每 200 次轮询打印一次 STATUS 心跳 */
    if ((poll_cnt % 200) == 0) {
        Safe_USB_Printf("[MAX30003][DBG] STATUS=0x%06lX, irq=%lu, samples=%lu, poll=%lu\r\n",
                   status_reg, ecg_irq_count, sample_count, poll_cnt);
    }

    /* 最高优先级：处理 FIFO overflow (数据手册要求 EOVF 后必须 FIFO_RST 或 SYNCH) */
    if (status_reg & MAX30003_STATUS_EOVF) {
        ovf_count++;

        Safe_USB_Printf("[MAX30003][WARN] EOVF, STATUS=0x%06lX, ovf=%lu\r\n", status_reg, ovf_count);

        MAX30003_FifoReset();
        MAX30003_Synch();

        uint32_t dummy = 0;
        MAX30003_ReadReg(MAX30003_STATUS, &dummy);
        return;
    }

    /* PLLINT 仅限频报警，不可在此处 return，否则 FIFO 数据无法清空导致溢出 */
    if (status_reg & MAX30003_STATUS_PLLINT) {
        pll_warn_count++;

        if (pll_warn_count <= 3 || (pll_warn_count % 200) == 0) {
            Safe_USB_Printf("[MAX30003][WARN] PLLINT observed. STATUS=0x%06lX, pll=%lu\r\n",
                       status_reg, pll_warn_count);
        }
    }

    if ((status_reg & MAX30003_STATUS_EINT) == 0) {
        return;
    }

    Safe_USB_Printf("[MAX30003][DBG] EINT detected, reading FIFO...\r\n");

    uint32_t samples[FIFO_BURST_SIZE];

    if (MAX30003_ReadFifoBurst(samples, FIFO_BURST_SIZE) != HAL_OK) {
        Safe_USB_Printf("[MAX30003][ERR] FIFO burst read failed\r\n");
        return;
    }

    for (uint8_t i = 0; i < FIFO_BURST_SIZE; i++) {
        uint32_t raw_data = samples[i];
        uint8_t etag = (raw_data >> 3) & 0x07;

        if (etag == 0x00 || etag == 0x02) {
            int16_t ecg_val = MAX30003_ConvertData(raw_data);

            /* 非阻塞/轻量缓存函数，切勿在此进行重度 I/O 操作 */
            Packagedata_AddEcgSample(ecg_val);
            sample_count++;

            /* 每 50 个有效样本打印一次 */
            if ((sample_count % 50) == 0) {
                Safe_USB_Printf("[ECG] sample=%lu, val=%d, etag=0x%02X, raw=0x%06lX\r\n",
                           sample_count, ecg_val, etag, raw_data);
            }

            if (etag == 0x02) { // Last Valid Sample (EOF)
                break;
            }
        }
        else if (etag == 0x01 || etag == 0x03) {
            /* Fast mode sample：时间有效，但电压值无效。当前直接丢弃电压。 */
            if (etag == 0x03) { // Last Fast Mode Sample (EOF)
                break;
            }
        }
        else if (etag == 0x06) {
            /* FIFO Empty */
            Safe_USB_Printf("[MAX30003][DBG] ETAG=0x06 FIFO empty\r\n");
            break;
        }
        else if (etag == 0x07) {
            /* FIFO Overflow tag */
            ovf_count++;

            Safe_USB_Printf("[MAX30003][WARN] ETAG overflow, reset FIFO. ovf=%lu\r\n", ovf_count);

            MAX30003_FifoReset();
            MAX30003_Synch();
            break;
        }
        else {
            Safe_USB_Printf("[MAX30003][DBG] unknown ETAG=0x%02X, raw=0x%06lX\r\n", etag, raw_data);
            break;
        }
    }

    /* 每 512 个样本打印一次心跳状态，确认采集流水线正常 */
    if ((sample_count % 512) == 0 && sample_count != 0) {
        Safe_USB_Printf("[MAX30003] ECG samples=%lu, ovf=%lu, pll=%lu\r\n",
                   sample_count, ovf_count, pll_warn_count);
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

void MAX30003_Diagnostic_Dump(void)
{
    uint32_t reg[5];

    Safe_USB_Printf("\r\n======================================================\r\n");
    Safe_USB_Printf("         MAX30003 COMPREHENSIVE DIAGNOSTIC            \r\n");
    Safe_USB_Printf("======================================================\r\n");

    /* Read core configuration registers */
    MAX30003_ReadReg(MAX30003_INFO, &reg[0]);
    MAX30003_ReadReg(MAX30003_CNFG_GEN, &reg[1]);
    MAX30003_ReadReg(MAX30003_CNFG_EMUX, &reg[2]);
    MAX30003_ReadReg(MAX30003_CNFG_ECG, &reg[3]);
    MAX30003_ReadReg(MAX30003_CNFG_CAL, &reg[4]);

    Safe_USB_Printf("[0x0F] INFO       : 0x%06X\r\n", (unsigned int)reg[0]);
    Safe_USB_Printf("[0x10] CNFG_GEN   : 0x%06X\r\n", (unsigned int)reg[1]);
    Safe_USB_Printf("[0x14] CNFG_EMUX  : 0x%06X (OPENP=%lu, OPENN=%lu)\r\n",
               (unsigned int)reg[2], (reg[2]>>21)&1, (reg[2]>>20)&1);
    Safe_USB_Printf("[0x15] CNFG_ECG   : 0x%06X\r\n", (unsigned int)reg[3]);
    Safe_USB_Printf("[0x12] CNFG_CAL   : 0x%06X\r\n", (unsigned int)reg[4]);

    Safe_USB_Printf("\r\n--- Executing Active DC Lead-Off Test ---\r\n");
    Safe_USB_Printf("Checking physical PCB trace continuity...\r\n");

    /*
     * Enable DC Lead-Off for testing:
     * EN_DCLOFF=01 (bits 13:12), IMAG=010 for 10nA (bits 10:8)
     */
    MAX30003_WriteReg(MAX30003_CNFG_GEN, MAX30003_CNFG_GEN_NORMAL | (1UL << 12) | (2UL << 8));

    /* Wait 200ms for internal comparators to stabilize (Datasheet requires > 115ms) */
    HAL_Delay(200);

    uint32_t status_reg;
    MAX30003_ReadReg(MAX30003_STATUS, &status_reg);
    Safe_USB_Printf("[0x01] STATUS     : 0x%06X\r\n", (unsigned int)status_reg);

    uint8_t loff = status_reg & 0x0F;
    Safe_USB_Printf("\r\n[HARDWARE DIAGNOSIS RESULT]:\r\n");
    if (loff == 0) {
        Safe_USB_Printf(" -> CLOSED: Both electrodes are physically connected.\r\n");
    } else {
        if (loff & 0x08) Safe_USB_Printf(" -> [FAIL] ECGP (Positive / Pin 6) is physically OPEN or floating!\r\n");
        if (loff & 0x04) Safe_USB_Printf(" -> [FAIL] ECGP (Positive / Pin 6) is SHORTED to GND!\r\n");
        if (loff & 0x02) Safe_USB_Printf(" -> [FAIL] ECGN (Negative / Pin 7) is physically OPEN or floating!\r\n");
        if (loff & 0x01) Safe_USB_Printf(" -> [FAIL] ECGN (Negative / Pin 7) is SHORTED to GND!\r\n");
    }

    /* Restore normal clean AFE configuration */
    MAX30003_WriteReg(MAX30003_CNFG_GEN, MAX30003_CNFG_GEN_NORMAL);

    Safe_USB_Printf("======================================================\r\n");
}