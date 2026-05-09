#ifndef __ECG_SD_LOGGER_H__
#define __ECG_SD_LOGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "cmsis_os.h"

typedef struct {
    uint32_t timestamp_ms;
    uint32_t sample_time_ms;
    uint32_t seq;
    int16_t ecg;
} ECG_SD_Record_t;

extern osMessageQueueId_t Q_ECG_SDHandle;

void ECG_SDLogger_InitQueue(void);
void StartTask_ECG_SDWriter(void *argument);
void ECG_SDLogger_Enqueue(int16_t ecg);
void ECG_SDLogger_RequestStop(void);
void ECG_SDLogger_ClearQueue(void);
uint32_t ECG_SDLogger_GetQueueCount(void);
uint32_t ECG_SDLogger_GetQueueCapacity(void);

#ifdef __cplusplus
}
#endif

#endif /* __ECG_SD_LOGGER_H__ */
