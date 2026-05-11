#ifndef __MAX3003_H__
#define __MAX3003_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* ========== 片选引脚定义 ========== */
/* 以 main.h 中 CubeMX 生成的引脚定义为准 */
/* 用 do{...}while(0) 更安全，避免 if/else 宏展开问题 */
	
#define ECG_CSB_LOW()   do { HAL_GPIO_WritePin(ECG_CS_GPIO_Port, ECG_CS_Pin, GPIO_PIN_RESET); } while (0)
#define ECG_CSB_HIGH()  do { HAL_GPIO_WritePin(ECG_CS_GPIO_Port, ECG_CS_Pin, GPIO_PIN_SET);   } while (0)

/* ========== MAX30003 寄存器地址 ========== */
/* 基本控制 */
#define MAX30003_NO_OP        0x00  /* 空操作（No Operation，占位命令） */
#define MAX30003_STATUS       0x01  /* 状态（只读，中断/FIFO状态） */
#define MAX30003_EN_INT       0x02  /* 中断1使能（INTB） */
#define MAX30003_EN_INT2      0x03  /* 中断2使能（INT2B） */
#define MAX30003_MNGR_INT     0x04  /* 中断管理 */
#define MAX30003_MNGR_DYN     0x05  /* 动态管理/快速恢复 */

/* 控制命令 */
#define MAX30003_SW_RST       0x08  /* 软件复位（写入触发） */
#define MAX30003_SYNCH        0x09  /* 同步内部时序 */
#define MAX30003_FIFO_RST     0x0A  /* FIFO复位（清空） */
#define MAX30003_INFO         0x0F  /* 器件信息（版本ID/芯片ID，只读） */

/* 配置寄存器 */
#define MAX30003_CNFG_GEN     0x10  /* 通用配置（时钟/偏置/通道使能等） */
#define MAX30003_CNFG_CAL     0x12  /* 校准配置（测试信号幅值/频率） */
#define MAX30003_CNFG_EMUX    0x14  /* 电极复用/导联检测配置 */
#define MAX30003_CNFG_ECG     0x15  /* ECG通道配置（增益/滤波） */
#define MAX30003_CNFG_RTOR1   0x1D  /* R-R检测配置1 */
#define MAX30003_CNFG_RTOR2   0x1E  /* R-R检测配置2 */

/* 数据寄存器 */
#define MAX30003_ECG_FIFO     0x20  /* ECG数据FIFO（18位ADC结果） */
#define MAX30003_PACE         0x21  /* 起搏器检测结果 */
#define MAX30003_RTOR         0x25  /* R-R间期结果（心率） */

/* 备用 */
#define MAX30003_NO_OP_ALT    0x7F  /* 备用空操作 */

/* ========== FIFO / Burst 读取 ========== */
#define FIFO_BURST_SIZE                32
#define MAX30003_ECG_FIFO_BURST       0x20  /* Burst 模式地址 */

/* ========== STATUS 寄存器关键位 ========== */
#define MAX30003_STATUS_EINT        0x800000UL
#define MAX30003_STATUS_EOVF        0x400000UL
#define MAX30003_STATUS_DCLOFFINT   (1UL << 20)
#define MAX30003_STATUS_PLLINT      0x000100UL
#define MAX30003_STATUS_LDOFF_PH    (1UL << 3)
#define MAX30003_STATUS_LDOFF_PL    (1UL << 2)
#define MAX30003_STATUS_LDOFF_NH    (1UL << 1)
#define MAX30003_STATUS_LDOFF_NL    (1UL << 0)

/* ========== 内部校准测试模式 ========== */
/* 设置为 1 启用内部 1Hz 校准波，0 为正常外部 ECG 输入 */
#define MAX30003_USE_INTERNAL_CAL_TEST    0

/* ========== CNFG_CAL 校准配置 ========== */
/* FCAL=00 (1Hz), SCAL=010 (0.5mV), CDONE=0, DF=0, FCD=00 */
#define MAX30003_CNFG_CAL_1HZ_BIPOLAR      0x704800UL

/* ========== CNFG_EMUX 电极复用 ========== */
/* CALP_SEL=11 (VCALP), CALN_SEL=01 (VCALN), CAL_MODE=11 */
#define MAX30003_CNFG_EMUX_CAL_DIFF        0x3B0000UL

/* ========== CNFG_GEN 位域 ========== */
#define CNFG_GEN_EN_ECG             (1UL << 19)
#define CNFG_GEN_FMSTR_32K          (0UL << 20)
#define CNFG_GEN_EN_DCLOFF_ECGPN    (1UL << 12)
#define CNFG_GEN_DCLOFF_IPOL_PU_ND  (0UL << 11)
#define CNFG_GEN_DCLOFF_IMAG_10NA   (2UL << 8)
#define CNFG_GEN_DCLOFF_IMAG_20NA   (3UL << 8)
#define CNFG_GEN_DCLOFF_VTH_300MV   (0UL << 6)
#define CNFG_GEN_EN_RBIAS_EN        (1UL << 4)
#define CNFG_GEN_RBIASV_100M        (1UL << 2)
#define CNFG_GEN_RBIASP_EN          (1UL << 1)
#define CNFG_GEN_RBIASN_EN          (1UL << 0)
/* 正常配置: ECG + DC Lead-Off 永久启用 (10nA, ±300mV) */
#define MAX30003_CNFG_GEN_NORMAL (CNFG_GEN_EN_ECG | CNFG_GEN_FMSTR_32K | \
                                  CNFG_GEN_EN_DCLOFF_ECGPN | \
                                  CNFG_GEN_DCLOFF_IPOL_PU_ND | \
                                  CNFG_GEN_DCLOFF_IMAG_10NA | \
                                  CNFG_GEN_DCLOFF_VTH_300MV | \
                                  CNFG_GEN_EN_RBIAS_EN | CNFG_GEN_RBIASV_100M | \
                                  CNFG_GEN_RBIASP_EN | CNFG_GEN_RBIASN_EN)

/* ========== CNFG_ECG 位域 ========== */
/* RATE=00 (512 SPS), GAIN=00 (20x), DHPF=1 (0.5Hz), DLPF=01 (40Hz) 抗50Hz工频 */
#define CNFG_ECG_RATE_512SPS    (0UL << 22)
#define CNFG_ECG_GAIN_40X       (1UL << 16)
#define CNFG_ECG_DHPF_0_5HZ     (1UL << 14)
#define CNFG_ECG_DLPF_40HZ      (1UL << 12)
#define MAX30003_CNFG_ECG_NORMAL (CNFG_ECG_RATE_512SPS | CNFG_ECG_GAIN_40X | \
                                  CNFG_ECG_DHPF_0_5HZ | CNFG_ECG_DLPF_40HZ)

/* ========== MNGR_DYN 配置 ========== */
/* FAST[1:0]=10 (Auto Fast Recovery), FAST_TH[5:0]=0x3F */
#define MAX30003_MNGR_DYN_AUTO_FAST  0xBF0000UL

/* ========== EN_INT 配置 ========== */
#define MAX30003_EN_INT_IDLE    0x000002UL   /* INTB_TYPE=10 (Open-Drain), 暂不使能 EINT/EOVF */
#define MAX30003_EN_INT_NORMAL  0xC00002UL   /* EN_EINT=1, EN_EOVF=1, INTB_TYPE=10 */

/* ========== MNGR_INT 配置 ========== */
#define MAX30003_MNGR_INT_FAST  (4UL << 19)  /* EFIT=4, 约5个样本触发一次 EINT */

/* ========== SPI 命令打包工具（可选） ========== */
/* R/Wb 在最低位：0=WRITE, 1=READ；reg: 0x00..0x7F */
static inline uint8_t MAX30003_MakeCmd(uint8_t reg, uint8_t isRead)
{
    return (uint8_t)(((reg & 0x7F) << 1) | (isRead ? 1U : 0U));
}

/* ========== 电极脱落检测状态 ========== */
typedef enum {
    MAX30003_LEAD_UNKNOWN = 0,
    MAX30003_LEAD_ON,
    MAX30003_LEAD_OFF
} MAX30003_LeadState_t;

typedef struct {
    MAX30003_LeadState_t state;
    uint8_t p_off;       /* ECGP 脱落 */
    uint8_t n_off;       /* ECGN 脱落 */
    uint8_t dc_loff;     /* DCLOFFINT */
    uint32_t raw_status;
    uint32_t last_update_ms;
} MAX30003_LeadStatus_t;

/* ========== 对外 API（在 max30003.c 实现） ========== */
HAL_StatusTypeDef MAX30003_WriteReg(uint8_t reg, uint32_t val24);
HAL_StatusTypeDef MAX30003_ReadReg(uint8_t reg, uint32_t *outVal24);

HAL_StatusTypeDef MAX3003_SoftwareReset(void);
HAL_StatusTypeDef MAX3003_ReadInfo(uint32_t *pInfo);
HAL_StatusTypeDef MAX3003_Synchronize(void);
HAL_StatusTypeDef MAX3003_ResetFIFO(void);

HAL_StatusTypeDef MAX3003_InitBasicECG(void);
HAL_StatusTypeDef MAX3003_ReadECGOnce(uint32_t *pRaw24);
int32_t           MAX3003_ConvertECG18(uint32_t raw24);
int16_t           MAX30003_ConvertData(uint32_t raw_data);

void MAX30003_Init(void);
void MAX30003_StartStream(void);
void MAX30003_StopStream(void);
void MAX30003_Task(void);
void MAX30003_SwReset(void);
void MAX30003_Synch(void);
void MAX30003_FifoReset(void);
int32_t MAX30003_Read_Sample(void);
void MAX30003_Diagnostic_Dump(void);
void MAX30003_UpdateLeadStatus(uint32_t status);
void MAX30003_GetLeadStatus(MAX30003_LeadStatus_t *out);
void MAX30003_PollLeadStatus(void);
#ifdef __cplusplus
}
#endif
#endif /* __MAX3003_H__ */
