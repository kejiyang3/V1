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
#include "usbd_cdc_if.h"
#include "usbd_core.h"
#include "ecg_record_control.h"
#include "ecg_sd_logger.h"
#include "ecg_usb_dump.h"
#include "app_log.h"
#include "sd_debug_log.h"
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
/* Definitions for Task_ECG_SDWriter */
osThreadId_t Task_ECG_SDWriterHandle;
const osThreadAttr_t Task_ECG_SDWriter_attributes = {
  .name = "Task_ECG_SDWriter",
  .stack_size = 1024 * 4,
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
static void APP_Print_ECG_Storage_Info(void);
/* USER CODE END FunctionPrototypes */

void StartTask_LVGL(void *argument);
void StartTask_Sensor(void *argument);
void StartTask_Button(void *argument);

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
  ECG_SDLogger_InitQueue();
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
  Task_ECG_SDWriterHandle = osThreadNew(StartTask_ECG_SDWriter, NULL, &Task_ECG_SDWriter_attributes);
  if (Task_ECG_SDWriterHandle == NULL) g_task_create_error |= (1UL << 2);
  /* USB Dump Task — V1 不创建，关闭 USB 文件发送 */
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

  MAX30003_Init();

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

        /* 等待 SDWriter 打开文件，最多 3000ms */
        uint32_t t0 = HAL_GetTick();
        while (!g_ecg_rec.sd_file_opened && (HAL_GetTick() - t0 < 3000)) {
            osDelay(10);
        }

        if (!g_ecg_rec.sd_file_opened) {
            g_ecg_rec.state = ECG_REC_ERROR;
            SD_DebugLog_WriteLine("ERROR_SD_OPEN_TIMEOUT");
            continue;
        }

        SD_DebugLog_WriteLine("CAL_TEST_START");

        ecg_buf_idx = 0;
        g_sys_state = SYS_STATE_RECORDING;

        /* 每次开始新记录前重置 MAX30003 相关统计 */
        ECG_ResetStats();

        while (ulTaskNotifyTake(pdTRUE, 0) > 0) {}
        __HAL_GPIO_EXTI_CLEAR_IT(ECG_INT_Pin);

        ecg_streaming = 1;
        MAX30003_StartStream();
      }
    }

    if (g_ecg_rec.request_stop) {
      g_ecg_rec.request_stop = 0;

      if (g_ecg_rec.state == ECG_REC_RECORDING) {
        ecg_streaming = 0;
        MAX30003_StopStream();
        ECG_SDLogger_RequestStop();
        g_ecg_rec.state = ECG_REC_STOPPING;

        SD_DebugLog_WriteLine("CAL_TEST_STOP_REQUEST");
      }
    }

    if (ecg_streaming && g_ecg_rec.state == ECG_REC_RECORDING) {
      (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
      MAX30003_Task();
    } else {
      osDelay(20);
    }

    if (g_ecg_rec.request_save_info) {
      g_ecg_rec.request_save_info = 0;
      SD_DebugLog_WriteSnapshot();
    }
  }
  /* USER CODE END StartTask_Sensor */
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

    /* SD 队列 */
    ECG_SDLogger_Enqueue(ecg);
  }
}

/**
  * @brief  打印 ECG 存储摘要信息 (USB Info 按钮)
  * @note   g_usb_info_press_count 定义在 app_lvgl.c，声明在 app_lvgl.h
  */
static void APP_Print_ECG_Storage_Info(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t duration = 0;
  char buf[512];

  if (g_ecg_rec.start_tick > 0) {
    duration = now - g_ecg_rec.start_tick;
  }

  int n = snprintf(buf, sizeof(buf),
    "\r\n[ECG_INFO] press=%lu\r\n"
    "state=%d\r\n"
    "file=%s\r\n"
    "duration_ms=%lu\r\n"
    "sample_count=%lu\r\n"
    "written_count=%lu\r\n"
    "drop_count=%lu\r\n"
    "sd_bytes=%lu\r\n"
    "sync_count=%lu\r\n"
    "ecg_irq=%lu\r\n"
    "tx_ok=%lu\r\n"
    "tx_busy=%lu\r\n"
    "tx_drop=%lu\r\n"
    "[/ECG_INFO]\r\n",
    g_usb_info_press_count,
    g_ecg_rec.state,
    g_ecg_rec.file_name,
    duration,
    g_ecg_rec.ecg_sample_count,
    g_ecg_rec.ecg_written_count,
    g_ecg_rec.ecg_drop_count,
    g_ecg_rec.sd_write_bytes,
    g_ecg_rec.sd_sync_count,
    ecg_irq_count,
    usb_tx_ok_count,
    usb_tx_busy_count,
    usb_tx_drop_count
  );

  if (n > 0) {
    Safe_USB_Printf("%s", buf);
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
