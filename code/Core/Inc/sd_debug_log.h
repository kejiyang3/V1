#ifndef __SD_DEBUG_LOG_H__
#define __SD_DEBUG_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void SD_DebugLog_Init(void);
void SD_DebugLog_WriteLine(const char *line);
void SD_DebugLog_WriteEvent(const char *tag, uint32_t value);
void SD_DebugLog_WriteSnapshot(void);

#ifdef __cplusplus
}
#endif

#endif /* __SD_DEBUG_LOG_H__ */
