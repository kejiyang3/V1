# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.
##语言
所有代码注释用中文，回答尽量用中文

## Build Commands

```bash
# Configure (first time or after CMakeLists.txt changes)
cd build && cmake .. -G Ninja

# Build
cd build && ninja

# Build from project root (if build.ninja exists)
ninja -C build
```

## Project Architecture

### Hardware
- **MCU**: STM32L496 (Cortex-M4)
- **ECG Sensor**: MAX30003 on SPI3 (512 SPS, 32-word FIFO burst)
- **Display**: ST7789V 240x280 LCD on SPI1 + DMA, backlight PWM (TIM3_CH4)
- **Touch**: CST816 capacitive touch on I2C2
- **USB**: CDC Virtual Serial Port (data export)
- **BLE**: UART1 + DMA (idle-line interrupt reception)
- **SD Card**: SDMMC + FatFS
- **Button**: PA1 (active-low, RC + software debounce)

### Data Flow ("Batch Record & Dump")
1. **MAX30003 interrupt** (EXTI falling edge) → wakes `Task_Sensor` via `ulTaskNotifyTake`
2. **`Task_Sensor`** (Realtime priority) calls `MAX30003_Task()` → burst reads 32 FIFO samples → calls `Packagedata_AddEcgSample()` for each
3. **Producer** (`Packagedata_AddEcgSample`) writes samples to `ecg_buffer[10240]` (20s @ 512 SPS, ~20KB RAM)
4. **Auto-trigger**: buffer full (10240 samples) → sets `SYS_STATE_DUMPING` → `osThreadFlagsSet(Task_EDFHandle, 0x01)`
5. **Consumer** (`Task_EDF`): wakes → dumps buffer via `Safe_USB_Printf` in 16-sample chunks → resets buffer/state

### FreeRTOS Tasks (by priority)
| Task | Priority | Role |
|------|----------|------|
| Task_Sensor | Realtime | MAX30003 FIFO burst read (5ms polling fallback) |
| Task_Audio | Normal1 | Idle (placeholder) |
| Task_LVGL | Low | USB init → LVGL init → lv_timer_handler() @ 5ms |
| Task_BLE | Low7 | UART1 command polling (10ms) |
| Task_Button | BelowNormal | PA1 debounce + dump trigger |
| Task_EDF | BelowNormal7 | USB metadata/EDF dump consumer |

### USB TX Safety (`Safe_USB_Printf`)
- Static buffer + lazy mutex init
- Checks `dev_state == USBD_STATE_CONFIGURED` before TX
- Waits for `hcdc->TxState == 0` before TX **and** after TX (blocks until transfer completes)
- Max 50ms wait each direction, force-clears TxState on timeout

### LVGL Button → Dump Bridge
- `APP_LVGL_GetBufferCount()` / `APP_LVGL_IsDumping()` / `APP_LVGL_TriggerEcgDump()` in freertos.c
- Screen button in App/LVGL/app_lvgl.c calls these via `LV_EVENT_CLICKED`
- Physical button (PA1) in StartTask_Button does the same logic independently

### Key Files
- `Core/Src/freertos.c` — All RTOS tasks, Safe_USB_Printf, dump trigger functions
- `Core/Src/max3003.c` — MAX30003 SPI driver, FIFO burst read, ETAG parsing
- `Core/Inc/max3003.h` — Register map, bitfield defines, CNFG_GEN/ECG macros
- `App/LVGL/app_lvgl.c` — LVGL init, ECG export button UI, status update timer
- `Core/Src/main.c` — HAL init, peripheral handles, interrupt handlers
- `Core/Inc/FreeRTOSConfig.h` — Stack sizes, priorities, heap config (61440 bytes)

### Python Tools
- `python/readECG.py` — Read binary ECG data (`ecg.bin`), plot time + frequency domain with 50Hz notch filter
- `python/stm32_cdc_monitor.py` — USB CDC serial capture for live monitoring
- `python/visualize_ecg.py` — Real-time plotting
- Run: `python python/readECG.py path/to/ecg.bin`
