#ifndef __ECG_USB_DUMP_H__
#define __ECG_USB_DUMP_H__

#ifdef __cplusplus
extern "C" {
#endif

void ECG_USB_RequestDump(void);
void StartTask_ECG_USBDump(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __ECG_USB_DUMP_H__ */
