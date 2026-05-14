/* USER CODE BEGIN Header */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "usb_printf.h"
#include "stm32l4xx_hal_uart.h"
#include "app_lvgl.h"
#include "max3003.h"
#include "max30102.h"
#include "icm20948.h"
#include "usbd_cdc_if.h"
#include "usbd_core.h"
#include "ecg_record_control.h"
#include "ecg_usb_dump.h"
#include "app_log.h"
#include "sd_debug_log.h"
#include "multi_sensor_logger.h"
/* USER CODE END Includes */
/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* External variables from main.c */
extern volatile uint8_t ecg_streaming;
extern volatile uint32_t ecg_irq_count;
extern uint8_t ble_rx_buf[];
extern volatile uint16_t ble_rx_len;
extern volatile uint8_t ble_rx_flag;
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;

TaskHandle_t EcgTaskHandle = NULL;             /* ECG任务句柄, 供ISR直接通知 */
TaskHandle_t PpgTaskHandle = NULL;             /* PPG任务句柄 */
TaskHandle_t ImuTaskHandle = NULL;             /* IMU任务句柄 */

/* ECG 临时 RAM 缓存 (短期观察用，不用作长期存储) */
#define ECG_BUFFER_SIZE 10240
int16_t ecg_buffer[ECG_BUFFER_SIZE];
volatile uint32_t ecg_buf_idx = 0;

typedef enum {
    SYS_STATE_IDLE = 0,
    SYS_STATE_RECORDING
} SysState_t;

volatile SysState_t g_sys_state = SYS_STATE_IDLE;
/* USB TX 统计计数器 */
volatile uint32_t usb_tx_ok_count = 0;
volatile uint32_t usb_tx_busy_count = 0;
volatile uint32_t usb_tx_drop_count = 0;

/* 任务创建错误掩码 (bit0=LVGL, bit1=Sensor, bit2=SDWriter, bit3=USBDump, bit4=Audio, bit5=Button, bit6=BLE) */
volatile uint32_t g_task_create_error = 0;
/* USER CODE END Variables */

/* Definitions for Task_LVGL */
osThreadId_t Task_LVGLHandle;
const osThreadAttr_t Task_LVGL_attributes = {
  .name = "Task_LVGL",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for Task_Sensor */
osThreadId_t Task_SensorHandle;
const osThreadAttr_t Task_Sensor_attributes = {
  .name = "Task_Sensor",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Task_Audio */
osThreadId_t Task_AudioHandle;
const osThreadAttr_t Task_Audio_attributes = {
  .name = "Task_Audio",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal1,
};
/* Definitions for Task_Button */
osThreadId_t Task_ButtonHandle;
const osThreadAttr_t Task_Button_attributes = {
  .name = "Task_Button",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Task_BLE */
osThreadId_t Task_BLEHandle;
const osThreadAttr_t Task_BLE_attributes = {
  .name = "Task_BLE",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow7,
};
/* Definitions for Task_PPG */
osThreadId_t Task_PPGHandle;
const osThreadAttr_t Task_PPG_attributes = {
  .name = "Task_PPG",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_IMU */
osThreadId_t Task_IMUHandle;
const osThreadAttr_t Task_IMU_attributes = {
  .name = "Task_IMU",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_MultiSensor_SDWriter */
osThreadId_t Task_MultiSensor_SDWriterHandle;
const osThreadAttr_t Task_MultiSensor_SDWriter_attributes = {
  .name = "Task_MSWriter",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Task_ECG_USBDump */
osThreadId_t Task_ECG_USBDumpHandle;
const osThreadAttr_t Task_ECG_USBDump_attributes = {
  .name = "Task_ECG_USBDump",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for Mtx_SDCard */
osMutexId_t Mtx_SDCardHandle;
const osMutexAttr_t Mtx_SDCard_attributes = {
  .name = "Mtx_SDCard"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void Safe_USB_Printf(const char *format, ...);
static void APP_ICM20948_InterruptSelfTest(void);
/* USER CODE END FunctionPrototypes */

void StartTask_LVGL(void *argument);
void StartTask_Sensor(void *argument);
void StartTask_Button(void *argument);
void StartTask_PPG(void *argument);
void StartTask_IMU(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void);

/**
  * @brief  FreeRTOS initialization
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Create the mutex(es) */
  Mtx_SDCardHandle = osMutexNew(&Mtx_SDCard_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  MultiSensorLogger_InitQueue();
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Task_LVGL */
  Task_LVGLHandle = osThreadNew(StartTask_LVGL, NULL, &Task_LVGL_attributes);
  if (Task_LVGLHandle == NULL) g_task_create_error |= (1UL << 0);

  /* creation of Task_Sensor */
  Task_SensorHandle = osThreadNew(StartTask_Sensor, NULL, &Task_Sensor_attributes);
  if (Task_SensorHandle == NULL) g_task_create_error |= (1UL << 1);

  /* creation of Task_Button */
  Task_ButtonHandle = osThreadNew(StartTask_Button, NULL, &Task_Button_attributes);
  if (Task_ButtonHandle == NULL) g_task_create_error |= (1UL << 5);

  /* USER CODE BEGIN RTOS_THREADS */
  /* PPG 采集任务 */
  Task_PPGHandle = osThreadNew(StartTask_PPG, NULL, &Task_PPG_attributes);
  if (Task_PPGHandle == NULL) g_task_create_error |= (1UL << 6);

  /* IMU 采集任务 */
  Task_IMUHandle = osThreadNew(StartTask_IMU, NULL, &Task_IMU_attributes);
  if (Task_IMUHandle == NULL) g_task_create_error |= (1UL << 7);

  /* 多传感器 SD Writer (取代旧的 ECG_SDWriter) */
  Task_MultiSensor_SDWriterHandle = osThreadNew(StartTask_MultiSensor_SDWriter, NULL, &Task_MultiSensor_SDWriter_attributes);
  if (Task_MultiSensor_SDWriterHandle == NULL) g_task_create_error |= (1UL << 2);

  /* USB Dump Task — V1 不创建 */
  /* USER CODE END RTOS_THREADS */
}

/* USER CODE BEGIN Header_StartTask_LVGL */
/* USER CODE END Header_StartTask_LVGL */
void StartTask_LVGL(void *argument)
{
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartTask_LVGL */
  (void)argument;

  /* 1. 等待 USB 枚举完成 */
  osDelay(2000);

  /* 2. 启动蓝牙 UART 空闲中断 DMA 接收 */
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, ble_rx_buf, BLE_RX_BUF_SIZE);
  __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);

  /* 3. USB CDC 就绪（V1 关闭 USB 日志） */
  APP_USB_LOG("\r\n[SYS] RTOS Started, USB CDC Ready!\r\n");

  /* 4. 初始化 LVGL 显示 + V1 ECG 控制界面 */
  APP_LVGL_Init();

  for(;;) {
    APP_LVGL_Process();
    osDelay(5);
  }
  /* USER CODE END StartTask_LVGL */
}

/* USER CODE BEGIN Header_StartTask_Sensor */
/* USER CODE END Header_StartTask_Sensor */
void StartTask_Sensor(void *argument)
{
  /* USER CODE BEGIN StartTask_Sensor */
  (void)argument;

  EcgTaskHandle = xTaskGetCurrentTaskHandle();

  osDelay(3000);

  /* SD debug log — 必须在 RTOS 运行后才能安全调用 f_mount */
  SD_DebugLog_Init();

  APP_USB_LOG("[ECG_V1] Sensor task started\r\n");

  /* I2C3 传感器一次性初始化 (失败不影响 ECG) */
  MAX30102_InitResult_t ppg_ret = MAX30102_Init();
  uint8_t icm_ret = ICM20948_Init();

  Safe_USB_Printf("\r\n[SENSOR_INIT]\r\n");
  if (ppg_ret == MAX30102_INIT_OK)
      Safe_USB_Printf("[MAX30102] I2C CALL OK, INIT OK\r\n");
  else if (ppg_ret == MAX30102_INIT_NOT_FOUND)
      Safe_USB_Printf("[MAX30102] I2C CALL FAIL, DEVICE NOT FOUND\r\n");
  else if (ppg_ret == MAX30102_INIT_CONFIG_FAILED)
      Safe_USB_Printf("[MAX30102] I2C CALL OK BUT CONFIG FAILED\r\n");
  else
      Safe_USB_Printf("[MAX30102] UNKNOWN INIT RESULT=%d\r\n", (int)ppg_ret);

  if (icm_ret == 0) {
      Safe_USB_Printf("[ICM20948] INIT OK (WHO_AM_I + 10x probe passed)\r\n");
  } else {
      Safe_USB_Printf("[ICM20948] INIT FAILED, RET=%u"
                      " (1=WHO_AM_I,2=PWR,3=Bank2,4=INT_CFG,5=VerifyRd,6=VerifyVal,7=10xWHO)\r\n",
                      (unsigned int)icm_ret);
  }
  Safe_USB_Printf("[/SENSOR_INIT]\r\n");

  if (ppg_ret == MAX30102_INIT_OK)          SD_DebugLog_WriteLine("MAX30102_INIT_OK");
  else if (ppg_ret == MAX30102_INIT_NOT_FOUND) SD_DebugLog_WriteLine("MAX30102_INIT_NOT_FOUND");
  else                                       SD_DebugLog_WriteLine("MAX30102_INIT_CONFIG_FAILED");

  if (icm_ret == 0) {
      SD_DebugLog_WriteLine("ICM20948_INIT_OK");
  } else {
      char icm_err[32];
      snprintf(icm_err, sizeof(icm_err), "ICM20948_INIT_FAILED,RET=%u", (unsigned int)icm_ret);
      SD_DebugLog_WriteLine(icm_err);
  }

  /* ICM20948 中断链路自检 — 仅在 ICM 初始化成功后执行一次 */
  if (icm_ret == 0) {
      APP_ICM20948_InterruptSelfTest();
  }

  /* ECG 初始化照旧，不受 PPG/ICM 影响 */
  MAX30003_Init();
  MAX30003_PollLeadStatus();
  SD_DebugLog_WriteLine("MAX30003_INIT_DONE");

  /* 初始不采集 */
  ecg_streaming = 0;
  g_sys_state = SYS_STATE_IDLE;

  for (;;) {
    if (g_ecg_rec.request_start) {
      g_ecg_rec.request_start = 0;

      if (g_ecg_rec.state == ECG_REC_IDLE ||
          g_ecg_rec.state == ECG_REC_STOPPED ||
          g_ecg_rec.state == ECG_REC_ERROR) {

        g_ecg_rec.state = ECG_REC_RECORDING;

        MultiSensorLogger_ResetForNewRecording();

        /* 等待 MSWriter 打开文件，最多 3000ms */
        uint32_t t0 = HAL_GetTick();
        while (!g_ecg_rec.sd_file_opened && (HAL_GetTick() - t0 < 3000)) {
            osDelay(10);
        }

        if (!g_ecg_rec.sd_file_opened) {
            g_ecg_rec.state = ECG_REC_ERROR;
            SD_DebugLog_WriteLine("ERROR_SD_OPEN_TIMEOUT");
            continue;
        }

        SD_DebugLog_WriteLine("RECORD_START");

        ecg_buf_idx = 0;
        g_sys_state = SYS_STATE_RECORDING;
        ECG_ResetStats();

        while (ulTaskNotifyTake(pdTRUE, 0) > 0) {}
        __HAL_GPIO_EXTI_CLEAR_IT(ECG_INT_Pin);
        __HAL_GPIO_EXTI_CLEAR_IT(PPG_INT_Pin);
        __HAL_GPIO_EXTI_CLEAR_IT(ICM_INT_Pin);

        {
            uint8_t s1, s2;
            MAX30102_ClearInterruptStatus(&s1, &s2);
        }
        ICM20948_ClearInterruptStatus();

        MAX30102_EnableFifoAlmostFullInterrupt();
        ICM20948_EnableDataReadyInterrupt();

        ecg_streaming = 1;
        MAX30003_StartStream();
      }
    }

    if (g_ecg_rec.request_stop) {
      g_ecg_rec.request_stop = 0;

      if (g_ecg_rec.state == ECG_REC_RECORDING) {
        ecg_streaming = 0;
        MAX30003_StopStream();

        MAX30102_DisableInterrupts();
        ICM20948_DisableDataReadyInterrupt();

        MultiSensorLogger_RequestStopAndFlush();
        g_ecg_rec.state = ECG_REC_STOPPING;

        SD_DebugLog_WriteLine("RECORD_STOP_REQUEST");
      }
    }

    if (ecg_streaming && g_ecg_rec.state == ECG_REC_RECORDING) {
      (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
      MAX30003_Task();
    } else {
      osDelay(20);
    }

    /* 低频轮询电极状态 (4Hz)，Idle 也持续检测 */
    static uint32_t last_lead_poll = 0;
    if (HAL_GetTick() - last_lead_poll >= 250) {
        last_lead_poll = HAL_GetTick();
        MAX30003_PollLeadStatus();
    }

    if (g_ecg_rec.request_save_info) {
      g_ecg_rec.request_save_info = 0;
      SD_DebugLog_WriteSnapshot();
    }
  }
  /* USER CODE END StartTask_Sensor */
}

/* USER CODE BEGIN Header_StartTask_PPG */
/* USER CODE END Header_StartTask_PPG */
void StartTask_PPG(void *argument)
{
  /* USER CODE BEGIN StartTask_PPG */
  (void)argument;
  PpgTaskHandle = xTaskGetCurrentTaskHandle();

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (g_ecg_rec.state != ECG_REC_RECORDING) {
      continue;
    }

    uint32_t ir_buf[32];
    uint32_t red_buf[32];

    uint8_t n = MAX30102_ReadFIFO_Batch(ir_buf, red_buf, 32);

    for (uint8_t i = 0; i < n; i++) {
      MultiSensorLogger_AddPPG(ir_buf[i], red_buf[i]);
    }
  }
  /* USER CODE END StartTask_PPG */
}

/* USER CODE BEGIN Header_StartTask_IMU */
/* USER CODE END Header_StartTask_IMU */
void StartTask_IMU(void *argument)
{
  /* USER CODE BEGIN StartTask_IMU */
  (void)argument;
  ImuTaskHandle = xTaskGetCurrentTaskHandle();

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (g_ecg_rec.state != ECG_REC_RECORDING) {
      continue;
    }

    int16_t ax, ay, az, gx, gy, gz;

    if (ICM20948_ReadAccelGyroRaw(&ax, &ay, &az, &gx, &gy, &gz) == 0) {
      MultiSensorLogger_AddIMU(ax, ay, az, gx, gy, gz);
    }
  }
  /* USER CODE END StartTask_IMU */
}

/* USER CODE BEGIN Header_StartTask_Audio */
/* USER CODE END Header_StartTask_Audio */
void StartTask_Audio(void *argument)
{
  (void)argument;
  for(;;) {
    osDelay(1000);
  }
}

/* USER CODE BEGIN Header_StartTask_Button */
/* USER CODE END Header_StartTask_Button */
void StartTask_Button(void *argument)
{
  /* USER CODE BEGIN StartTask_Button */
  (void)argument;
  uint8_t prev = 1;

  for(;;) {
      uint8_t curr = HAL_GPIO_ReadPin(KEY_BTN_GPIO_Port, KEY_BTN_Pin);

      if (prev == 1 && curr == 0) {
          osDelay(20);
          if (HAL_GPIO_ReadPin(KEY_BTN_GPIO_Port, KEY_BTN_Pin) == 0) {
              /* 物理按键：toggle start/stop */
              if (g_ecg_rec.state == ECG_REC_IDLE ||
                  g_ecg_rec.state == ECG_REC_STOPPED ||
                  g_ecg_rec.state == ECG_REC_ERROR) {
                  ECG_RequestStart();
              } else if (g_ecg_rec.state == ECG_REC_RECORDING) {
                  ECG_RequestStop();
              }
          }
      }
      prev = curr;
      osDelay(20);
  }
  /* USER CODE END StartTask_Button */
}

/* USER CODE BEGIN Header_StartTask_BLE */
/* USER CODE END Header_StartTask_BLE */
void StartTask_BLE(void *argument)
{
  /* USER CODE BEGIN StartTask_BLE */
  (void)argument;

  for(;;) {
    if (ble_rx_flag) {
      if (ble_rx_len < BLE_RX_BUF_SIZE) {
        ble_rx_buf[ble_rx_len] = '\0';
      } else {
        ble_rx_buf[BLE_RX_BUF_SIZE - 1] = '\0';
      }
      ble_rx_flag = 0;
      ble_rx_len = 0;
    }

    osDelay(10);
  }
  /* USER CODE END StartTask_BLE */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief  ICM20948 中断链路自检 — latched mode, 仅在开机时执行一次
  * @note   使用锁存中断模式 (INT_PIN_CFG=0xE0) 避免 50µs 窄脉冲不可见。
  *         结果同时输出到 SD debug_log 和 USB CDC (一次性)。
  */
static void APP_ICM20948_InterruptSelfTest(void)
{
    char line[256];
    int16_t ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;

    uint32_t irq0, irq1, irq2, irq3;
    uint8_t whoami, pwr1, pwr2, pin_cfg_before, int_en1_before;
    uint8_t st_before, st_after_wait1, st_after_read, st_after_wait2;
    uint8_t raw_ret;
    GPIO_PinState pin_before, pin_after_wait1, pin_after_read, pin_after_wait2;

    HAL_StatusTypeDef s_who, s_pwr1, s_pwr2, s_cfg, s_en1, s_st0;
    HAL_StatusTypeDef s_st1, s_st2, s_st3;

    extern volatile uint32_t icm_irq_count;

#define RD_TAG(s)  (((s) == HAL_OK) ? "RD_OK" : "RD_FAIL")

    SD_DebugLog_WriteLine("ICM_IRQ_SELFTEST_BEGIN");
    Safe_USB_Printf("\r\n[ICM_IRQ_SELFTEST_BEGIN]\r\n");

    /* T0: 初始读回全部关键寄存器 (checked) + PH1 电平 + IRQ 计数 */
    s_who  = ICM20948_ReadBank0Reg_Checked(0x00, &whoami);
    s_pwr1 = ICM20948_ReadBank0Reg_Checked(0x06, &pwr1);
    s_pwr2 = ICM20948_ReadBank0Reg_Checked(0x07, &pwr2);
    s_cfg  = ICM20948_ReadBank0Reg_Checked(0x0F, &pin_cfg_before);
    s_en1  = ICM20948_ReadBank0Reg_Checked(0x11, &int_en1_before);
    s_st0  = ICM20948_ReadBank0Reg_Checked(0x1A, &st_before);
    pin_before = HAL_GPIO_ReadPin(ICM_INT_GPIO_Port, ICM_INT_Pin);
    irq0       = icm_irq_count;

    snprintf(line, sizeof(line),
             "ICM_T0,WHO=0x%02X(%s),PWR1=0x%02X(%s),PWR2=0x%02X(%s),CFG=0x%02X(%s),EN1=0x%02X(%s),ST1=0x%02X(%s),PIN=%u,IRQ=%lu",
             whoami, RD_TAG(s_who),
             pwr1, RD_TAG(s_pwr1),
             pwr2, RD_TAG(s_pwr2),
             pin_cfg_before, RD_TAG(s_cfg),
             int_en1_before, RD_TAG(s_en1),
             st_before, RD_TAG(s_st0),
             (unsigned)pin_before, (unsigned long)irq0);
    SD_DebugLog_WriteLine(line);
    Safe_USB_Printf("%s\r\n", line);

    /* T1: 清旧状态，开启锁存 Data Ready 中断，等待 120ms */
    ICM20948_ClearInterruptStatus();
    ICM20948_EnableLatchedDataReadyInterrupt_Debug();
    osDelay(120);

    s_st1          = ICM20948_ReadBank0Reg_Checked(0x1A, &st_after_wait1);
    pin_after_wait1 = HAL_GPIO_ReadPin(ICM_INT_GPIO_Port, ICM_INT_Pin);
    irq1            = icm_irq_count;

    snprintf(line, sizeof(line),
             "ICM_T1_AFTER_120MS,ST1=0x%02X(%s),PIN=%u,IRQ=%lu",
             st_after_wait1, RD_TAG(s_st1),
             (unsigned)pin_after_wait1, (unsigned long)irq1);
    SD_DebugLog_WriteLine(line);
    Safe_USB_Printf("%s\r\n", line);

    /* T2: 读一次六轴原始数据 (ReadAccelGyroRaw 末尾会 ClearInterruptStatus) */
    raw_ret = ICM20948_ReadAccelGyroRaw(&ax, &ay, &az, &gx, &gy, &gz);

    s_st2          = ICM20948_ReadBank0Reg_Checked(0x1A, &st_after_read);
    pin_after_read = HAL_GPIO_ReadPin(ICM_INT_GPIO_Port, ICM_INT_Pin);
    irq2           = icm_irq_count;

    if (raw_ret == 0) {
        snprintf(line, sizeof(line),
                 "ICM_T2_AFTER_READ,RET=0,AX=%d,AY=%d,AZ=%d,GX=%d,GY=%d,GZ=%d,ST1=0x%02X(%s),PIN=%u,IRQ=%lu",
                 ax, ay, az, gx, gy, gz,
                 st_after_read, RD_TAG(s_st2),
                 (unsigned)pin_after_read, (unsigned long)irq2);
    } else {
        snprintf(line, sizeof(line),
                 "ICM_T2_AFTER_READ,RET=%u,AX=NA,AY=NA,AZ=NA,GX=NA,GY=NA,GZ=NA,ST1=0x%02X(%s),PIN=%u,IRQ=%lu",
                 (unsigned)raw_ret,
                 st_after_read, RD_TAG(s_st2),
                 (unsigned)pin_after_read, (unsigned long)irq2);
    }
    SD_DebugLog_WriteLine(line);
    Safe_USB_Printf("%s\r\n", line);

    /* T3: 读完后再等 120ms，看 latched 模式是否再次产生中断 */
    osDelay(120);

    s_st3           = ICM20948_ReadBank0Reg_Checked(0x1A, &st_after_wait2);
    pin_after_wait2 = HAL_GPIO_ReadPin(ICM_INT_GPIO_Port, ICM_INT_Pin);
    irq3            = icm_irq_count;

    snprintf(line, sizeof(line),
             "ICM_T3_SECOND_120MS,ST1=0x%02X(%s),PIN=%u,IRQ=%lu",
             st_after_wait2, RD_TAG(s_st3),
             (unsigned)pin_after_wait2, (unsigned long)irq3);
    SD_DebugLog_WriteLine(line);
    Safe_USB_Printf("%s\r\n", line);

    /* 自检结束：恢复为禁用中断，避免影响后续正常录制流程 */
    ICM20948_DisableDataReadyInterrupt();
    ICM20948_ClearInterruptStatus();

    SD_DebugLog_WriteLine("ICM_IRQ_SELFTEST_END");
    Safe_USB_Printf("[ICM_IRQ_SELFTEST_END]\r\n");

#undef RD_TAG
}

/**
  * @brief  Producer: 将 ECG 样本存入 RAM buffer 和 SD 队列
  */
void Packagedata_AddEcgSample(int16_t ecg)
{
  if (g_ecg_rec.state != ECG_REC_RECORDING) {
    return;
  }

  if (g_sys_state == SYS_STATE_RECORDING) {
    /* RAM 缓存（短期观察） */
    if (ecg_buf_idx < ECG_BUFFER_SIZE) {
      ecg_buffer[ecg_buf_idx++] = ecg;
    }

    /* 多传感器 block logger (取代旧 ECG_SDLogger_Enqueue) */
    MultiSensorLogger_AddECG(ecg);
  }
}

/**
  * @brief  Safe_USB_Printf — 调试日志打印
  * @note   使用 CDC_Transmit_FS_Blocking 阻塞发送，带 200ms 超时。
  *         仅在采样停止后使用，采样进行中不要高频调用。
  */
void Safe_USB_Printf(const char *format, ...)
{
  char buf[384];
  extern USBD_HandleTypeDef hUsbDeviceFS;

  if (format == NULL) return;

  /* USB 未配置时直接丢弃 */
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED ||
      hUsbDeviceFS.pClassData == NULL) {
    usb_tx_drop_count++;
    return;
  }

  /* 格式化 */
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (len <= 0) return;
  if (len >= (int)sizeof(buf)) {
    len = (int)sizeof(buf) - 1;
    buf[len] = '\0';
  }

  /* 阻塞发送 (200ms 超时) */
  uint8_t ret = CDC_Transmit_FS_Blocking((uint8_t*)buf, (uint16_t)len, 200);
  if (ret == USBD_OK) {
    usb_tx_ok_count++;
  } else if (ret == USBD_BUSY) {
    usb_tx_busy_count++;
  } else {
    usb_tx_drop_count++;
  }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  taskDISABLE_INTERRUPTS();
  while (1) { __NOP(); }
}

void vApplicationMallocFailedHook(void)
{
  taskDISABLE_INTERRUPTS();
  while (1) { __NOP(); }
}
/* USER CODE END Application */
