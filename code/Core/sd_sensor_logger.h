#ifndef __SD_SENSOR_LOGGER_H__
#define __SD_SENSOR_LOGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include "sensor_record.h"

extern osMessageQueueId_t Q_SensorRecordHandle;

void SDLogger_InitQueue(void);
void StartTask_SDWriter(void *argument);
void SDLogger_EnqueueFromTask(const SensorRecord_t *rec);

#ifdef __cplusplus
}
#endif

#endif /* __SD_SENSOR_LOGGER_H__ */