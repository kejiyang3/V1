#ifndef __APP_LOG_H__
#define __APP_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * V1 阶段关闭 USB 日志。
 * USB CDC 当前不稳定，不再作为主要调试手段。
 */
#define USB_LOG_ENABLE    0

#if USB_LOG_ENABLE
void Safe_USB_Printf(const char *format, ...);
#define APP_USB_LOG(...)  Safe_USB_Printf(__VA_ARGS__)
#else
#define APP_USB_LOG(...)  do {} while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __APP_LOG_H__ */
