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

# Build from project root
ninja -C build
```

## Project Architecture

### Hardware
- **MCU**: STM32L496 (Cortex-M4)
- **ECG Sensor**: MAX30003 on SPI3 (512 SPS, 32-word FIFO burst)
- **PPG Sensor**: MAX30102 on I2C3 (200 SPS, 18-bit IR+RED, FIFO A_FULL interrupt)
- **IMU Sensor**: ICM20948 on I2C3 (51 Hz, 6-axis accel+gyro, Data Ready interrupt)
- **Level Shifter**: TXS0104ERGYR on I2C3 bus
- **Display**: ST7789V 240x280 LCD on SPI1 + DMA, backlight PWM (TIM3_CH4)
- **Touch**: CST816 capacitive touch on I2C2
- **USB**: CDC Virtual Serial Port (unstable under high frequency output)
- **BLE**: UART1 + DMA (idle-line interrupt)
- **SD Card**: SDMMC + FatFS
- **Button**: PA1 (active-low, software debounce)

### Pin Mapping (3 Interrupt Lines)
| Pin | EXTI | Sensor | Signal |
|-----|------|--------|--------|
| PB6 | EXTI9_5 | MAX30003 | ECG INTB (falling edge) |
| PH1 | EXTI1 | MAX30102 | PPG A_FULL (falling edge) |
| PC2 | EXTI2 | ICM20948 | IMU Data Ready (falling edge) |

### Interrupt → Task Notification Chain
```
HAL_GPIO_EXTI_Callback (main.c):
  ECG_INT_Pin → ecg_irq_count++ → xTaskNotifyGive(EcgTaskHandle)
  PPG_INT_Pin → ppg_irq_count++ → xTaskNotifyGive(PpgTaskHandle)
  ICM_INT_Pin → icm_irq_count++ → xTaskNotifyGive(ImuTaskHandle)

ISR 只做: 累加 irq_count + 通知任务。不读 I2C/SPI/SD。
```

### FreeRTOS Tasks
| Task | Priority | Stack | Role |
|------|----------|-------|------|
| Task_Sensor | AboveNormal | 8KB | ECG MAX30003 FIFO read + Start/Stop control + PPG diag log |
| Task_PPG | Normal | 4KB | PPG MAX30102 FIFO read + SD storage via MultiSensorLogger |
| Task_IMU | Normal | 4KB | ICM20948 6-axis raw data read |
| Task_MSWriter | BelowNormal | 8KB | Multi-sensor 2s block CSV writer |
| Task_PPGDiagWr | BelowNormal7 | 2KB | PPG INT diagnostic CSV writer (0:/ppg_int_diag.csv) |
| Task_LVGL | Low | 8KB | USB init → LVGL UI @ 5ms |
| Task_Button | BelowNormal | 1KB | PA1 debounce → Start/Stop toggle |
| Task_BLE | Low7 | 1KB | UART1 command polling |
| Task_Audio | Normal1 | 1KB | Idle placeholder |

### IRQ Counters (in main.c)
- `volatile uint32_t ecg_irq_count` — ECG INTB total
- `volatile uint32_t ppg_irq_count` — PPG A_FULL total
- `volatile uint32_t icm_irq_count` — ICM Data Ready total

### Data Flow — Multi-Sensor Block Logging
1. **Start** → `MultiSensorLogger_ResetForNewRecording()` → enable 3 interrupts
2. Each sensor task reads data → calls `MultiSensorLogger_AddXXX()` per sample
3. Samples fill 2-second double-buffer blocks (ECG=1024, PPG=400, IMU=104 samples)
4. Full block → `submit_xxx_block()` → `MS_BlockMsg_t` queue (depth 12)
5. `Task_MSWriter`: opens CSV → writes header → dequeues blocks → `f_write` → `f_sync` every 8 blocks
6. **Stop** → `RequestStopAndFlush()` flushes partial blocks → `f_sync` + `f_close` **(no f_mount(NULL))** to avoid unmount race with PPGDiagWriter

### Recording Start/Stop Flow
- **Start**: clear notifyCount → clear EXTI pending → `MAX30102_ClearInterruptStatus` → `ICM20948_ClearInterruptStatus` → `MAX30102_EnableFifoAlmostFullInterrupt` → `ICM20948_EnableDataReadyInterrupt` → `ecg_streaming=1` → `MAX30003_StartStream`
- **Stop**: `ecg_streaming=0` → `MAX30003_StopStream` → `MAX30102_DisableInterrupts` → `ICM20948_DisableDataReadyInterrupt` → `MultiSensorLogger_RequestStopAndFlush` → state = ECG_REC_STOPPING

### MAX30102 INT Pin Protocol (Critical)
- MAX30102 INT is **open-drain, active low**. After power-up, PWR_RDY asserts INT low.
- Reading INTERRUPT_STATUS1 (0x00) **clears the interrupt status and releases INT pin back HIGH**.
- MAX30102_ReadFIFO_Batch reads STATUS1 at entry to release INT, then reads FIFO data.
- Without reading STATUS1 or FIFO_DATA, INT stays low forever — EXTI only triggers once.
- When enabling A_FULL via `INTERRUPT_ENABLE1 = 0x80`, if FIFO already has data, INT immediately goes LOW. This falling edge is normal.

### SD Write Safety
- `multi_sensor_logger.c`: all `f_write` calls use `sd_write_checked()` — checks `FRESULT` and `bw==len`. Write ok/fail counted separately per sensor type.
- Two SD Writers co-exist (main CSV + PPG diag CSV), sharing `Mtx_SDCardHandle` mutex.
- **Never** `f_mount(NULL, ...)` in stop/error paths — the other writer may have a file open on the same volume.
- `MS_SYNC_EVERY_BLOCKS=8`, `PPG_DIAG_SYNC_EVERY=8` — fast sync for short recordings.
- PPG diag writer does extra `f_sync` on `ECG_REC_STOPPING`/`STOPPED` state.

### LVGL UI Pages
- **Page 1 (Main)**: ECG state, lead status, sample rate, file name, Start/Stop button
- **Page 2 (Diagnostic)**: STATUS register, PLL/EOVF/Written, PPG IRQ/INT, IE1/IS1, FIFO W/R/OV, MODE
- Swipe left/right to switch pages

### Key Files
| File | Content |
|------|---------|
| `Core/Src/freertos.c` | All RTOS tasks, Safe_USB_Printf, sensor init, PPG diag, ICM self-test |
| `Core/Src/main.c` | HAL init, GPIO EXTI callback, IRQ counters, ICM line test |
| `Core/Src/max3003.c` | MAX30003 SPI driver, FIFO burst read, lead-off detection |
| `Core/Src/Max30102.c` | MAX30102 I2C driver, FIFO batch read, interrupt enable/disable |
| `Core/Src/icm20948.c` | ICM20948 I2C driver, bank select, 6-axis read, checked I2C helpers |
| `Core/Src/multi_sensor_logger.c` | 2s block double-buffer + MS_BlockMsg queue + CSV writer |
| `Core/Src/sd_sensor_logger.c` | PPG INT diag queue + writer (`0:/ppg_int_diag.csv`) |
| `Core/Src/sd_debug_log.c` | SD card debug log (`0:/debug_log.txt`) |
| `Core/Src/gpio.c` | GPIO config: PPG=PH1 IT_FALLING, ICM=PC2 IT_FALLING, ECG=PB6 IT_FALLING |
| `Core/Src/stm32l4xx_it.c` | EXTI handlers: EXTI1→PPG, EXTI2→ICM, EXTI9_5→ECG |
| `Core/Inc/main.h` | Pin macros, `ICM_INT_LINE_PULLDOWN_TEST_ENABLE` switch |
| `Core/Inc/sensor_record.h` | `SensorRecord_t` with ECG/PPG/IMU/PPG_INT_DIAG union types |
| `Core/Inc/app_log.h` | USB logging gate: `USB_LOG_ENABLE` — set 1 for diagnostic prints |
| `App/LVGL/app_lvgl.c` | LVGL 2-page UI with PPG diag labels |
| `Core/Inc/FreeRTOSConfig.h` | Stack sizes, priorities, heap: 61440 bytes |
| `CMakeLists.txt` | Build config (root = real build) |
| `code/CMakeLists.txt` | Build config copy for GitHub publishing |

### Debug Macros
- `ICM_INT_LINE_PULLDOWN_TEST_ENABLE` (`main.h`) — set `1` for PH1 open-drain pulldown test (2s LOW/2s HIGH loop before RTOS)
- `USB_LOG_ENABLE` (`app_log.h`) — set `1` to enable USB CDC diagnostic prints via `APP_USB_LOG()`. **USB CDC unstable under sustained high frequency output** — use only for short diagnostic bursts.
- `ICM20948_ENABLE_MAG_MASTER` (`icm20948.h`) — set `0` (magnetometer disabled)

### Build Warnings (pre-existing, not fixable)
- `-Wunused-parameter` in HAL/ThirdParty/FatFs driver files
- `-Wunused-function` in `sd_sensor_logger.c` (old `SDLogger_TypeToString`)
- `-Wunused-variable` in `lv_port_indev.c`

### Python Tools
- `python/readECG.py` — Read binary ECG data, plot time + frequency domain
- `python/stm32_cdc_monitor.py` — USB CDC serial capture
- `python/visualize_ecg.py` — Real-time plotting
