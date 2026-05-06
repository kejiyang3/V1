# 调试说明

## TXS0104ERGYR 电平转换

| 引脚 | A侧(1.8V) | B侧(3.3V) |
|------|-----------|-----------|
| I2C_SCL | 传感器SCL | STM32 PC0 |
| I2C_SDA | 传感器SDA | STM32 PC1 |
| PPG_INT | MAX30102 INT | STM32 PC2 |
| ICM_INT | ICM20948 INT | STM32 PH1 |

- VCCA = 1.8V, VCCB = 3.3V
- OE 必须拉高到 VCCA (1.8V)
- GND 共地

## PPG_INT / ICM_INT 诊断

**测量方法**：用示波器/万用表分别测 A 侧和 B 侧。

| A侧 | B侧 | 判断 |
|-----|-----|------|
| LOW | LOW | 传感器自身拉低（pending interrupt 未清） |
| HIGH | LOW | TXS/OE/STM32 引脚问题 |
| LOW | HIGH | TXS 通道异常 |
| HIGH | HIGH | 硬件正常 |

## 当前调试阶段配置

- ICM_INT + PPG_INT: GPIO_MODE_INPUT + PULLUP（非 EXTI）
- MAX30102: 中断全部禁用（INT_ENABLE1/2 = 0x00）
- ICM20948: 中断全部禁用（INT_ENABLE_1/2/3 = 0x00），INT_PIN_CFG = 0xC0 (active-low open-drain)
- MAX30003_StartStream: 简化版（无 FIFO_RST/SYNCH）
- ECG_INT: 保留 EXTI falling edge

## g_debug_step 状态码

| 值 | 含义 |
|----|------|
| 10 | USB 就绪 |
| 100 | Sensor task 进入 |
| 110 | 初始化 ICM20948 |
| 120 | ICM20948 完成 |
| 130 | 初始化 MAX30102 |
| 140 | MAX30102 完成 |
| 150 | PPG/ICM 中断清除 |
| 200 | 初始化 MAX30003 |
| 210 | MAX30003 完成 |
| 220 | 准备 StartStream |
| 230 | 进入 StartStream |
| 240 | 离开 StartStream |
| 250 | 首次 MAX30003_Task |
| 260 | ECG 循环正常运行 |

## 日志预期

```
[SYS] RTOS Started, USB CDC Ready!
[SENSOR] task entered
[INT_LEVEL][before_sensor_init] PPG_INT=HIGH, ICM_INT=HIGH
[SENSOR] step 1: before ICM20948_Init
[ICM20948] interrupts disabled and status cleared for debug
[SENSOR] step 2: ICM20948_Init OK
[INT_LEVEL][after_icm_init] PPG_INT=HIGH, ICM_INT=LOW/HIGH
[SENSOR] step 3: before MAX30102_Init
[MAX30102] interrupts disabled and status cleared for debug
[SENSOR] step 4: after MAX30102_Init
[INT_CLEAR] ...
[INT_LEVEL][after_clear_ppg_icm_int] PPG_INT=HIGH, ICM_INT=HIGH
[SENSOR] step 5: before MAX30003_Init
[MAX30003] Initializing...
[MAX30003] Init done.
[SENSOR] step 6: after MAX30003_Init
[START] step 1~5: StartStream
[SENSOR] step 10: entering ECG loop
[HEARTBEAT] tick=... step=260 ...
[ECG_TASK] alive call=200 STATUS=...
