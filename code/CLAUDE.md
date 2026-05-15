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
- **USB**: CDC Virtual Serial Port
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
| Task_PPG | Normal | 4KB | PPG MAX30102 FIFO read + USB real-time print |
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
5. `Task_MSWriter`: opens CSV → writes header → dequeues blocks → `f_write` → `f_sync` every 512 writes
6. **Stop** → `RequestStopAndFlush()` flushes partial blocks → close file

### PPG USB Real-Time Output
- `StartTask_PPG`: `ulTaskNotifyTake` → `MAX30102_ReadFIFO_Batch` → per-sample `Safe_USB_Printf("PPG,seq,tick,ir,red\r\n")`
- ~200 lines/sec during recording
- SD writing temporarily disabled for PPG to ensure USB stability

### LVGL UI Pages
- **Page 1 (Main)**: ECG state, lead status, sample rate, file name, Start/Stop button
- **Page 2 (Diagnostic)**: STATUS register, PLL/EOVF/Written, PPG IRQ/INT, IE1/IS1, FIFO W/R/OV, MODE
- Swipe left/right to switch pages

### MAX30102 ReadFIFO_Batch (Diagnostic Mode)
- Reads INTERRUPT_STATUS1 to release INT pin but does NOT bail on A_FULL bit=0
- Always computes FIFO available count from WR/RD pointers
- Returns 0 only if no data available

### ICM20948 Init Verification
- Every I2C write checked for HAL_OK
- Post-init: re-reads WHO_AM_I, PWR1, PWR2, INT_PIN_CFG
- 10x WHO_AM_I probe at 20ms intervals before declaring success
- `ICM20948_ENABLE_MAG_MASTER=0` — magnetometer bypassed, 6-axis only

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
| `App/LVGL/app_lvgl.c` | LVGL 2-page UI with PPG diag labels |
| `Core/Inc/FreeRTOSConfig.h` | Stack sizes, priorities, heap: 61440 bytes |

### Build Warnings (pre-existing, not fixable)
- `-Wunused-parameter` in HAL/ThirdParty/FatFs driver files
- `-Wunused-function` in `sd_sensor_logger.c` (old `SDLogger_TypeToString`)
- `-Wunused-variable` in `lv_port_indev.c`

### Temporary Debug Macros
- `ICM_INT_LINE_PULLDOWN_TEST_ENABLE` in `main.h` — set to `0` for normal operation
- MAX30003 code was temporarily commented out during MAX30102 debugging (now restored)

### Python Tools
- `python/readECG.py` — Read binary ECG data, plot time + frequency domain
- `python/stm32_cdc_monitor.py` — USB CDC serial capture
- `python/visualize_ecg.py` — Real-time plotting
