#ifndef __MAX3003_H__
#define __MAX3003_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* ========== 片选引脚定义 ========== */
#define ECG_CSB_LOW()   do { HAL_GPIO_WritePin(ECG_CS_GPIO_Port, ECG_CS_Pin, GPIO_PIN_RESET); } while (0)
#define ECG_CSB_HIGH()  do { HAL_GPIO_WritePin(ECG_CS_GPIO_Port, ECG_CS_Pin, GPIO_PIN_SET);   } while (0)

/* ========== 外部输入测试模式 ========== */
#define MAX30003_USE_INTERNAL_CAL_TEST    0

/* ========== 寄存器地址 ========== */
#define MAX30003_NO_OP        0x00
#define MAX30003_STATUS       0x01
#define MAX30003_EN_INT       0x02
#define MAX30003_EN_INT2      0x03
#define MAX30003_MNGR_INT     0x04
#define MAX30003_MNGR_DYN     0x05
#define MAX30003_SW_RST       0x08
#define MAX30003_SYNCH        0x09
#define MAX30003_FIFO_RST     0x0A
#define MAX30003_INFO         0x0F
#define MAX30003_CNFG_GEN     0x10
#define MAX30003_CNFG_CAL     0x12
#define MAX30003_CNFG_EMUX    0x14
#define MAX30003_CNFG_ECG     0x15
#define MAX30003_CNFG_RTOR1   0x1D
#define MAX30003_CNFG_RTOR2   0x1E
#define MAX30003_ECG_FIFO     0x20
#define MAX30003_PACE         0x21
#define MAX30003_RTOR         0x25
#define MAX30003_NO_OP_ALT    0x7F

/* ========== FIFO / Burst ========== */
#define FIFO_BURST_SIZE                32
#define MAX30003_ECG_FIFO_BURST       0x20

/* ========== STATUS 位 ========== */
#define MAX30003_STATUS_EINT    0x800000UL
#define MAX30003_STATUS_EOVF    0x400000UL
#define MAX30003_STATUS_PLLINT  0x000100UL

/* ========== CNFG_GEN ========== */
#define CNFG_GEN_EN_ECG         (1UL << 19)
#define CNFG_GEN_FMSTR_32K      (0UL << 20)
#define CNFG_GEN_EN_RBIAS_EN    (1UL << 4)
#define CNFG_GEN_RBIASV_100M    (1UL << 2)
#define CNFG_GEN_RBIASP_EN      (1UL << 1)
#define CNFG_GEN_RBIASN_EN      (1UL << 0)
#define MAX30003_CNFG_GEN_NORMAL (CNFG_GEN_EN_ECG | CNFG_GEN_FMSTR_32K | \
                                  CNFG_GEN_EN_RBIAS_EN | CNFG_GEN_RBIASV_100M | \
                                  CNFG_GEN_RBIASP_EN | CNFG_GEN_RBIASN_EN)

/* ========== CNFG_CAL ========== */
/* 外部输入模式: 禁用内部校准 */
#define MAX30003_CNFG_CAL_EXTERNAL      0x000000UL
/* 内部校准 (保留, 当前不用) */
#define MAX30003_CNFG_CAL_1HZ           0x704800UL

/* ========== CNFG_EMUX ========== */
/* 外部输入模式: ECGP/ECGN 直通 AFE */
#define MAX30003_CNFG_EMUX_EXTERNAL     0x000000UL
/* 内部校准 EMUX (保留) */
#define MAX30003_CNFG_EMUX_CAL_DIFF     0x3B0000UL

/* ========== CNFG_ECG ========== */
#define CNFG_ECG_RATE_512SPS    (0UL << 22)
#define CNFG_ECG_GAIN_20X       (0UL << 16)
#define CNFG_ECG_DHPF_0_5HZ     (1UL << 14)
#define CNFG_ECG_DLPF_100HZ     (2UL << 12)
#define MAX30003_CNFG_ECG_NORMAL (CNFG_ECG_RATE_512SPS | CNFG_ECG_GAIN_20X | \
                                  CNFG_ECG_DHPF_0_5HZ | CNFG_ECG_DLPF_100HZ)

/* ========== EN_INT ========== */
#define MAX30003_EN_INT_IDLE    0x000002UL   /* INTB_TYPE=10 open-drain, 不使能 EINT/EOVF */
#define MAX30003_EN_INT_NORMAL  0xC00002UL   /* EN_EINT=1, EN_EOVF=1, INTB_TYPE=10, 不含 PLLINT */

/* ========== MNGR_INT ========== */
#define MAX30003_MNGR_INT_FAST  (4UL << 19)  /* EFIT=4 */

/* ========== API ========== */
HAL_StatusTypeDef MAX30003_WriteReg(uint8_t reg, uint32_t val24);
HAL_StatusTypeDef MAX30003_ReadReg(uint8_t reg, uint32_t *outVal24);
int16_t           MAX30003_ConvertData(uint32_t raw_data);

void MAX30003_Init(void);
void MAX30003_StartStream(void);
void MAX30003_StopStream(void);
void MAX30003_Task(void);
void MAX30003_SwReset(void);
void MAX30003_Synch(void);
void MAX30003_FifoReset(void);
void MAX30003_Diagnostic_Dump(void);

uint8_t MAX30003_CheckExternalInputConfig(void);
void MAX30003_SaveRegisterSnapshotToDebugLog(const char *tag);

#ifdef __cplusplus
}
#endif
#endif /* __MAX3003_H__ */
