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
// #include "sd_wav_test.h"  /* SD卡任务已屏蔽 */
#include "max3003.h"
#include "usbd_cdc_if.h"
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
/* External variables from main.c for BLE communication */
extern uint8_t ble_rx_buf[];
extern volatile uint16_t ble_rx_len;
extern volatile uint8_t ble_rx_flag;
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;   /* for BLE DMA RX init */

/* Function prototypes */
void APP_Log(const char *format, ...);
void APP_BLE_ParseCommand(const char *cmd);

/* SD WAV test task — 已屏蔽 */
// osThreadId_t Task_SDWavTestHandle;
// const osThreadAttr_t Task_SDWavTest_attributes = {
//   .name = "Task_SDWavTest",
//   .stack_size = 512 * 4,
//   .priority = (osPriority_t) osPriorityBelowNormal,
// };

/* ===== MAX30003 中断驱动 (ETAG解析在 max3003.c 的 MAX30003_Task 中) ===== */
extern volatile uint8_t ecg_streaming;         /* 定义在 main.c */
TaskHandle_t EcgTaskHandle = NULL;             /* ECG任务句柄, 供ISR直接通知 */

/* ===== BATCH RECORD & DUMP ARCHITECTURE ===== */
#define ECG_BUFFER_SIZE 10240  /* 512 SPS * 20 seconds = 10240 samples */
int16_t ecg_buffer[ECG_BUFFER_SIZE];
volatile uint32_t ecg_buf_idx = 0;

typedef enum {
    SYS_STATE_RECORDING = 0,
    SYS_STATE_DUMPING
} SysState_t;

volatile SysState_t g_sys_state = SYS_STATE_RECORDING;
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
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Task_Audio */
osThreadId_t Task_AudioHandle;
const osThreadAttr_t Task_Audio_attributes = {
  .name = "Task_Audio",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal1,
};
/* Definitions for Task_EDF */
osThreadId_t Task_EDFHandle;
const osThreadAttr_t Task_EDF_attributes = {
  .name = "Task_EDF",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal7,
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
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow7,
};
/* Definitions for Mtx_SDCard */
osMutexId_t Mtx_SDCardHandle;
const osMutexAttr_t Mtx_SDCard_attributes = {
  .name = "Mtx_SDCard"
};
/* Definitions for EG_SystemState */
osEventFlagsId_t EG_SystemStateHandle;
const osEventFlagsAttr_t EG_SystemState_attributes = {
  .name = "EG_SystemState"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
// void StartTask_SDWavTest(void *argument);  /* SD卡任务已屏蔽 */
static void APP_Sensor_Comm_Test(void);
void Safe_USB_Printf(const char *format, ...);
/* USER CODE END FunctionPrototypes */

void StartTask_LVGL(void *argument);
void StartTask_Sensor(void *argument);
void StartTask_Audio(void *argument);
void StartTask_EDF(void *argument);
void StartTask_Button(void *argument);
void StartTask_BLE(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of Mtx_SDCard */
  Mtx_SDCardHandle = osMutexNew(&Mtx_SDCard_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* ECG ISR->Task同步改用 osThreadFlagsSet (无需信号量) */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* No queues needed in batch-record architecture */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Task_LVGL */
  Task_LVGLHandle = osThreadNew(StartTask_LVGL, NULL, &Task_LVGL_attributes);

  /* creation of Task_Sensor */
  Task_SensorHandle = osThreadNew(StartTask_Sensor, NULL, &Task_Sensor_attributes);

  /* creation of Task_Audio */
  Task_AudioHandle = osThreadNew(StartTask_Audio, NULL, &Task_Audio_attributes);

  /* creation of Task_EDF */
  Task_EDFHandle = osThreadNew(StartTask_EDF, NULL, &Task_EDF_attributes);

  /* creation of Task_Button */
  Task_ButtonHandle = osThreadNew(StartTask_Button, NULL, &Task_Button_attributes);

  /* creation of Task_BLE */
  Task_BLEHandle = osThreadNew(StartTask_BLE, NULL, &Task_BLE_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* Task_SDWavTest — SD卡任务已屏蔽 */
  // Task_SDWavTestHandle = osThreadNew(StartTask_SDWavTest, NULL, &Task_SDWavTest_attributes);

  /* Task_ECG 已移除 — ECG采集合并到 Task_Sensor */
  /* USER CODE END RTOS_THREADS */

  /* creation of EG_SystemState */
  EG_SystemStateHandle = osEventFlagsNew(&EG_SystemState_attributes);

  /* USER CODE BEGIN RTOS_EVENTS */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartTask_LVGL */
/* USER CODE END Header_StartTask_LVGL */
void StartTask_LVGL(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartTask_LVGL */
  (void)argument;

  /* 1. 等待 USB 枚举完成（PC 端识别虚拟串口约需 1-2 秒） */
  osDelay(2000);

  /* 2. 启动蓝牙 UART 空闲中断 DMA 接收（含 osDelay 操作，必须在 RTOS 启动后） */
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, ble_rx_buf, BLE_RX_BUF_SIZE);
  __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);

  /* 3. USB CDC 虚拟串口就绪，可以安全使用 usb_printf */
  usb_printf("\r\n[SYS] RTOS Started, USB CDC Ready!\r\n");

  /* 4. 初始化 LVGL 显示 + 触摸 + ECG 导出界面 */
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

  Safe_USB_Printf("[SENSOR] task entered\r\n");

  EcgTaskHandle = xTaskGetCurrentTaskHandle();

  /*
   * 避开 USB 枚举阶段。
   */
  osDelay(3000);
  Safe_USB_Printf("[SENSOR] before MAX30003_Init\r\n");

  MAX30003_Init();

  Safe_USB_Printf("[SENSOR] after MAX30003_Init\r\n");

  /*
   * 清掉可能残留的任务通知和 EXTI pending。
   */
  while (ulTaskNotifyTake(pdTRUE, 0) > 0) {
      /* drain stale notifications */
  }

  __HAL_GPIO_EXTI_CLEAR_IT(ECG_INT_Pin);

  /*
   * 先允许 ISR 通知，再启动 MAX30003 stream。
   */
  ecg_streaming = 1;
  Safe_USB_Printf("[SENSOR] before MAX30003_StartStream\r\n");

  MAX30003_StartStream();

  Safe_USB_Printf("[SENSOR] after MAX30003_StartStream\r\n");
  Safe_USB_Printf("[ECG] INT-Driven started. 5ms fallback polling enabled.\r\n");

  /*
   * 启动后主动读一次，防止刚开启时已有 EINT。
   */
  MAX30003_Task();

  for (;;)
  {
      /*
       * 512 SPS + 32-word FIFO 理论满载时间约 62.5 ms。
       * fallback 改为 5 ms，避免漏中断后堆满。
       */
      (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));

      MAX30003_Task();
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

/* USER CODE BEGIN Header_StartTask_EDF */
/* USER CODE END Header_StartTask_EDF */
void StartTask_EDF(void *argument)
{
  /* USER CODE BEGIN StartTask_EDF */
  (void)argument;
  char print_buf[512];

  for(;;) {
      /* 等待 buffer 满或按键触发 */
      osThreadFlagsWait(0x01, osFlagsWaitAny, osWaitForever);

      Safe_USB_Printf("\r\n=== DUMP START: %lu SAMPLES ===\r\n", ecg_buf_idx);

      uint32_t dump_count = ecg_buf_idx;
      uint16_t offset = 0;

      for (uint32_t i = 0; i < dump_count; i++) {
          int written = snprintf(print_buf + offset, sizeof(print_buf) - offset, "%d\r\n", ecg_buffer[i]);
          if (written > 0 && (offset + (uint16_t)written) < sizeof(print_buf)) {
              offset += written;
          }

          /* 每 16 个样本或最后样本刷一次 USB
           * Safe_USB_Printf 内已阻塞等待 Tx 完成，无需额外 Delay */
          if ((i + 1) % 16 == 0 || i == dump_count - 1) {
              Safe_USB_Printf("%s", print_buf);
              offset = 0;
          }
      }

      Safe_USB_Printf("=== DUMP END ===\r\n");

      ecg_buf_idx = 0;
      g_sys_state = SYS_STATE_RECORDING;
  }
  /* USER CODE END StartTask_EDF */
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

              if (g_sys_state == SYS_STATE_RECORDING && ecg_buf_idx > 0) {
                  Safe_USB_Printf("\r\n[BTN] Dump Triggered!\r\n");
                  g_sys_state = SYS_STATE_DUMPING;
                  osThreadFlagsSet(Task_EDFHandle, 0x01);
              }
          }
      }
      prev = curr;
      osDelay(20);
  }
  /* USER CODE END StartTask_Button */
}

/* USER CODE BEGIN Header_StartTask_BLE */
/**
* @brief Function implementing the Task_BLE thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_BLE */
void StartTask_BLE(void *argument)
{
  /* USER CODE BEGIN StartTask_BLE */
  (void)argument;
  APP_Log("BLE task started");

  for(;;) {
    /* 等待接收到蓝牙数据 */
    if (ble_rx_flag) {
      /* 确保数据以null结尾 */
      if (ble_rx_len < BLE_RX_BUF_SIZE) {
        ble_rx_buf[ble_rx_len] = '\0';
      } else {
        ble_rx_buf[BLE_RX_BUF_SIZE - 1] = '\0';
      }

      /* 解析命令 */
      APP_BLE_ParseCommand((const char *)ble_rx_buf);

      /* 清除标志位 */
      ble_rx_flag = 0;
      ble_rx_len = 0;
    }

    osDelay(10); /* 10ms polling interval */
  }
  /* USER CODE END StartTask_BLE */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief  Producer: 将样本存入 RAM buffer.
  * @note   覆盖 max3003.c 中的 weak 定义. 不可包含阻塞调用.
  */
void Packagedata_AddEcgSample(int16_t ecg)
{
    static uint32_t dbg_cnt = 0;

    if (g_sys_state == SYS_STATE_RECORDING) {
        if (ecg_buf_idx < ECG_BUFFER_SIZE) {
            ecg_buffer[ecg_buf_idx++] = ecg;
        }

        dbg_cnt++;

        if ((dbg_cnt % 50) == 0) {
            Safe_USB_Printf("[ECG_BUF] idx=%lu, val=%d\r\n", ecg_buf_idx, ecg);
        }

        /* 20s buffer 满了 → 自动触发 dump */
        if (ecg_buf_idx >= ECG_BUFFER_SIZE) {
            g_sys_state = SYS_STATE_DUMPING;
            osThreadFlagsSet(Task_EDFHandle, 0x01);
        }
    }
}

/**
  * @brief  RTOS-Safe USB CDC Print (Industrial Grade)
  * @note   Static buffer + mutex, 防止异步 USB Tx 读取失效栈内存.
  */
/**
  * @brief  RTOS-Safe USB CDC Print (Industrial Grade)
  * @note   Static buffer + mutex, 增加了空指针和 USB 掉线终极防护
  */
void Safe_USB_Printf(const char *format, ...)
{
    static char usb_tx_buf[512];
    static osMutexId_t usb_printf_mutex = NULL;

    /* 延迟初始化互斥锁 */
    if (usb_printf_mutex == NULL) {
        osMutexAttr_t attr = { .name = "USB_Printf_Mutex" };
        usb_printf_mutex = osMutexNew(&attr);
    }

    /* 1. 获取锁 (200ms 超时, 避免死等) */
    if (osMutexAcquire(usb_printf_mutex, pdMS_TO_TICKS(200)) != osOK) {
        return;
    }

    extern USBD_HandleTypeDef hUsbDeviceFS;

    /* 2. USB 掉线 / 未配置保护 */
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED || hUsbDeviceFS.pClassData == NULL) {
        osMutexRelease(usb_printf_mutex);
        return;
    }

    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;

    /* 3. 等待前一次传输完成 (Usb FS 帧周期 1ms, 50ms 足够) */
    int32_t retry = 50;
    while (hcdc->TxState != 0 && retry > 0) {
        osDelay(1);
        retry--;
    }
    if (hcdc->TxState != 0) {
        hcdc->TxState = 0;   /* 异常卡死才强制清除, 丢弃此包 */
        osMutexRelease(usb_printf_mutex);
        return;
    }

    /* 4. 格式化字符串 */
    va_list args;
    va_start(args, format);
    int len = vsnprintf(usb_tx_buf, sizeof(usb_tx_buf), format, args);
    va_end(args);

    /* 5. 触发发送, 然后等待本次传输完成再释放锁 */
    if (len > 0 && len < (int)sizeof(usb_tx_buf)) {
        CDC_Transmit_FS((uint8_t*)usb_tx_buf, len);

        /* 等待传输完成 — 保证 usb_tx_buf 在释放锁前不再被改写 */
        retry = 50;
        while (hcdc->TxState != 0 && retry > 0) {
            osDelay(1);
            retry--;
        }
    }

    osMutexRelease(usb_printf_mutex);
}
/**
  * @brief  Stack overflow hook — triggered by configCHECK_FOR_STACK_OVERFLOW 2
  * @note   Pure infinite loop — NO usb_printf to avoid nested crash.
  */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    taskDISABLE_INTERRUPTS();

    /* --- 栈溢出: 在此设断点, 查看 pcTaskName --- */
    while (1) {
        __NOP();
    }
}

/**
  * @brief  Malloc failed hook — triggered when pvPortMalloc returns NULL
  * @note   Pure infinite loop — NO usb_printf to avoid nested crash.
  */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();

    /* --- 内存分配失败: 在此设断点 --- */
    while (1) {
        __NOP();
    }
}

/**
  * @brief  Unified logging function (UART DMA)
  * @param  format: printf-style format string
  * @retval None
  */
void APP_Log(const char *format, ...)
{
  char buffer[256];
  char output[300];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  /* Format output with prefix and newline */
  snprintf(output, sizeof(output), "[BLE] %s\r\n", buffer);

  /* Send via UART DMA */
  uint32_t len = strlen(output);
  if (len > 0) {
    HAL_UART_Transmit_DMA(&huart1, (uint8_t*)output, len);

    /* Wait for DMA transfer to complete with timeout */
    uint32_t timeout = 1000; /* 1 second timeout */
    while (HAL_UART_GetState(&huart1) != HAL_UART_STATE_READY && timeout > 0) {
      osDelay(1); /* Delay 1ms */
      timeout--;
    }
  }
}

/**
  * @brief  Parse BLE command and execute corresponding action
  * @param  cmd: Command string (null-terminated)
  * @retval None
  */
void APP_BLE_ParseCommand(const char *cmd)
{
  APP_Log("Received: %s", cmd);

  if (strstr(cmd, "CMD:START_SAMP") != NULL) {
    APP_Log("Start sampling command received");
    /* 通过EventGroup通知Task_Sensor开始采样 */
    osEventFlagsSet(EG_SystemStateHandle, 0x01);
  }
  else if (strstr(cmd, "CMD:STOP_SAMP") != NULL) {
    APP_Log("Stop sampling command received");
    /* 通过EventGroup通知Task_Sensor停止采样 */
    osEventFlagsSet(EG_SystemStateHandle, 0x02);
  }
  else if (strstr(cmd, "CMD:SYNC_TIME:") != NULL) {
    const char *time_str = cmd + strlen("CMD:SYNC_TIME:");
    APP_Log("Sync time command received: %s", time_str);
    /* 这里可以添加RTC时间同步代码 */
  }
  else {
    APP_Log("Unknown command");
  }
}

/* ================================================================
 *  LVGL 屏幕按钮接口 — 供 app_lvgl.c 调用，替代物理按键
 * ================================================================ */

/**
  * @brief  返回当前已采集的 ECG 样本数
  */
uint32_t APP_LVGL_GetBufferCount(void)
{
    return ecg_buf_idx;
}

/**
  * @brief  返回当前是否正在 USB dump
  * @retval 0 = 采集中, 1 = dump 中
  */
uint8_t APP_LVGL_IsDumping(void)
{
    return (g_sys_state == SYS_STATE_DUMPING) ? 1 : 0;
}

/**
  * @brief  屏幕按钮按下：触发 ECG 数据 USB 导出
  * @note   替代物理按键 KEY_BTN (PA1) 的功能
  */
void APP_LVGL_TriggerEcgDump(void)
{
    if (g_sys_state == SYS_STATE_RECORDING && ecg_buf_idx > 0) {
        Safe_USB_Printf("\r\n[LVGL] Dump Triggered!\r\n");
        g_sys_state = SYS_STATE_DUMPING;
        osThreadFlagsSet(Task_EDFHandle, 0x01);
    }
}

/**
  * @brief  MAX30003 ECG sensor diagnostic
  * @note   Tests SPI3+MCO (MAX30003).
  *         Call once at startup — do NOT call repeatedly.
  */
static void APP_Sensor_Comm_Test(void)
{
    /* MAX30003 (SPI3 + MCO) */
    usb_printf("\r\n--- Testing MAX30003 (SPI3 + MCO) ---\r\n");
    MAX30003_Diagnostic_Dump();
}
/* USER CODE END Application */

