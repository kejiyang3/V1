#ifndef __SENSOR_RECORD_H__
#define __SENSOR_RECORD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

typedef enum {
    SENSOR_REC_ECG = 1,
    SENSOR_REC_PPG = 2,
    SENSOR_REC_IMU = 3,
    SENSOR_REC_PPG_INT_DIAG = 4
} SensorRecordType_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t seq;
    SensorRecordType_t type;

    union {
        struct {
            int16_t ecg;
        } ecg;

        struct {
            uint32_t ir;
            uint32_t red;
        } ppg;

        struct {
            int16_t ax;
            int16_t ay;
            int16_t az;
            int16_t gx;
            int16_t gy;
            int16_t gz;
        } imu;

        struct {
            uint32_t irq_count;
            uint8_t  pin_before;   /* 1=HIGH, 0=LOW */
            uint8_t  status1;      /* INTERRUPT_STATUS1 原始值 */
            uint8_t  pin_after;    /* 1=HIGH, 0=LOW */
            uint8_t  ie1;          /* INTERRUPT_ENABLE1 */
            uint8_t  fifo_wr;      /* FIFO_WR_POINTER & 0x1F */
            uint8_t  fifo_rd;      /* FIFO_RD_POINTER & 0x1F */
            uint8_t  fifo_ov;      /* FIFO_OV_COUNTER & 0x1F */
        } ppg_int_diag;
    } data;
} SensorRecord_t;

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_RECORD_H__ */