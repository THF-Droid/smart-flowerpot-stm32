/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 主程序体 - 智能花盆云监控系统
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * 本软件的许可条款可在该软件组件根目录下的LICENSE文件中找到。
  * 如果本软件未附带LICENSE文件，则按AS-IS方式提供。
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "stdio.h"
#include "OLED.h"
#include "dht11.h"
#include <math.h>   // 用于 floor() 函数
#include <string.h> // 用于 memset() 函数
#include "random_forest.h"   //随机森林规则引用
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LIGHT_THRESHOLD 2.5f      // 光照阈值（低于此值开启补光灯）
#define REFERENCE_VOLTAGE 3.3f    // 参考电压（ADC最大输入电压）
#define CONVERSION_FACTOR 1000    // 转换系数（需根据传感器校准，示例值）
#define SOIL_HUMIDITY_MAX 3.3f    // 最大湿度对应电压（干燥时）
#define SOIL_HUMIDITY_MIN 0.0f    // 最小湿度对应电压（湿润时）
// 新增风扇控制定义
#define TEMP_THRESHOLD 30.0f      // 温度阈值（例如30℃）
#define SOIL_HUMIDITY_THRESHOLD 30    // 土壤湿度阈值（低于此值启动浇水，单位%）
#define SERVO_THRESHOLD_1 1800      // 舵机光照阈值1（lux，>=此值舵机转90°）
#define SERVO_THRESHOLD_2 2800        // 舵机光照阈值2（lux，>=此值舵机转180°）
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

SPI_HandleTypeDef hspi2;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart6;  /* ==== ESP32 新增：USART6句柄 ==== */

/* Definitions for ADCTask */
osThreadId_t ADCTaskHandle;
const osThreadAttr_t ADCTask_attributes = {
  .name = "ADCTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for ComTask */
osThreadId_t ComTaskHandle;
const osThreadAttr_t ComTask_attributes = {
  .name = "ComTask",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for OledTask */
osThreadId_t OledTaskHandle;
const osThreadAttr_t OledTask_attributes = {
  .name = "OledTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for DHT11Task */
osThreadId_t DHT11TaskHandle;
const osThreadAttr_t DHT11Task_attributes = {
  .name = "DHT11Task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow1,
};
/* Definitions for SWCTask */
osThreadId_t SWCTaskHandle;
const osThreadAttr_t SWCTask_attributes = {
  .name = "SWCTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow2,
};
/* Definitions for ADCBinarySem */
osSemaphoreId_t ADCBinarySemHandle;
const osSemaphoreAttr_t ADCBinarySem_attributes = {
  .name = "ADCBinarySem"
};
/* Definitions for ADCCountingSem */
osSemaphoreId_t ADCCountingSemHandle;
const osSemaphoreAttr_t ADCCountingSem_attributes = {
  .name = "ADCCountingSem"
};
/* Definitions for DHT11CountingSem */
osSemaphoreId_t DHT11CountingSemHandle;
const osSemaphoreAttr_t DHT11CountingSem_attributes = {
  .name = "DHT11CountingSem"
};
/* Definitions for SWCCountingSem */
osSemaphoreId_t SWCCountingSemHandle;
const osSemaphoreAttr_t SWCCountingSem_attributes = {
  .name = "SWCCountingSem"
};
/* USER CODE BEGIN PV */
float ADC_VALUE = 0.0f;
float voltage = 0.0f;
float light_intensity = 0.0f;  // 光照强度（单位：lux）
char str1[16]={0};
char str2[16]={0};  // 用于显示光照强度

// AI推理相关
uint8_t ai_status = 0;            // 0=健康 1=缺水 2=光照不足 3=温度异常

// 蜂鸣器异步标志（解决中断里不能延时的死锁问题）
volatile uint8_t buzzer_trigger = 0;  // 0=无请求, N=需要响N声
volatile uint8_t buzzer_stop    = 0;  // 1=强制停止蜂鸣器（手动关闭）
volatile uint8_t buzzer_mute    = 0;  // 1=蜂鸣器静音中（BZOFF后30秒内AI不再自动触发）

/* 中断安全：命令响应延迟发送（ISR只设标志，ComTask负责串口回复） */
volatile uint8_t cmd_resp_pending = 0;    // 0=无待回复 1=有待回复
char    cmd_resp_text[64] = {0};          // 待回复的完整文本（ISR中写入，任务中读取）
UART_HandleTypeDef *cmd_resp_huart = NULL; // 目标串口指针（ISR中写入，任务中读取）

// 全局变量（用于任务间共享）
float dht11_temp = 0.0f;
float dht11_humi = 0.0f;
uint8_t dht11_read_status = 0; // 0:成功, 1:无响应, 2:校验失败
float soil_voltage = 0.0f;     // 土壤湿度传感器电压
uint8_t soil_humidity = 0;     // 土壤湿度百分比（0-100%）
char str3[16] = {0};           // 用于显示土壤湿度
uint8_t display_mode = 0; // 0:温湿度 1:光照 2:土壤湿度
// USART1（蓝牙）相关变量
uint8_t rx_buf1[1];          // USART1单字节接收缓冲区
char cmd_buf1[32];           // USART1指令缓冲区
uint8_t cmd_len1 = 0;        // USART1指令长度
uint8_t rx_buf2[1];          // USART2单字节接收缓冲区
uint8_t rx_buf6[1];          // USART6单字节接收缓冲区
char cmd_buf6[32];
uint8_t cmd_len6 = 0;
char cmd_buf2[32];           // USART2指令缓冲区
uint8_t cmd_len2 = 0;        // USART2指令长度
uint8_t manual_mode = 0;     // 0：自动模式，1：手动模式
/* 设备状态变量（用于JSON上报） */
uint8_t fan_state  = 0;     // 风扇状态（PC1）：0=关 1=开
uint8_t bulb_state = 0;     // 补光灯状态（PA5）：0=关 1=开
uint8_t pump_state = 0;     // 水泵状态（PA7）：0=关 1=开
uint8_t servo_angle = 90;   // 舵机当前角度（0/90/180）
void parse_command(char *cmd, UART_HandleTypeDef *huart);
// FreeRTOS互斥锁（防止串口发送冲突）
osMutexId_t UART_MutexHandle;
const osMutexAttr_t UART_Mutex_attributes = {
  .name = "UART_Mutex"
};

/* Definitions for AITask */
osThreadId_t AITaskHandle;
const osThreadAttr_t AITask_attributes = {
  .name = "AITask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_SPI2_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART6_UART_Init(void);  /* ==== ESP32 新增 ==== */
void StartADCTask(void *argument);
void StartComTask(void *argument);
void StartOledTask(void *argument);
void StartDHT11Task(void *argument);
void StartSWCTask(void *argument);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef* htim);
/* USER CODE BEGIN PFP */
void StartAITask(void *argument);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// ===== 舵机控制函数 =====
// 使用TIM1_CH1(PA8)输出PWM控制舵机角度
void Servo_SetAngle(uint8_t angle) {
    uint16_t pulse = 250 + (uint16_t)(angle * 1000UL / 180);
    if (pulse > 1250) pulse = 1250;
    if (pulse < 250)  pulse = 250;
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, pulse);
}

// ===== 蜂鸣器控制（低电平触发模块：RESET(0V)=响, SET(3.3V)=停）=====
void Buzzer_On(void) {
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET); // 低电平→蜂鸣器响
}
void Buzzer_Off(void) {
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);   // 高电平→蜂鸣器停
}

// ===== AI推理函数（占位，后续替换为随机森林规则） =====
PlantStatus AI_Predict(float temp, float humi, float soil, float light) {
    return random_forest_predict(temp, humi, soil, light);
}
/* USER CODE END 0 */


/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_SPI2_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_TIM1_Init();
  MX_USART6_UART_Init();  /* ==== ESP32 新增 ==== */
  /* USER CODE BEGIN 2 */
  OLED_Init();
  DHT11_Init();

  // 确保蜂鸣器初始为关闭状态（高电平=停）
  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);

  UART_MutexHandle = osMutexNew(&UART_Mutex_attributes);

  HAL_UART_Receive_IT(&huart1, rx_buf1, 1);
  HAL_UART_Receive_IT(&huart2, rx_buf2, 1);
  HAL_UART_Receive_IT(&huart6, rx_buf6, 1);

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  Servo_SetAngle(90);
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of ADCBinarySem */
  ADCBinarySemHandle = osSemaphoreNew(1, 1, &ADCBinarySem_attributes);

  /* creation of ADCCountingSem */
  ADCCountingSemHandle = osSemaphoreNew(2, 2, &ADCCountingSem_attributes);

  /* creation of DHT11CountingSem */
  DHT11CountingSemHandle = osSemaphoreNew(2, 2, &DHT11CountingSem_attributes);

  /* creation of SWCCountingSem */
  SWCCountingSemHandle = osSemaphoreNew(2, 2, &SWCCountingSem_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of ADCTask */
  ADCTaskHandle = osThreadNew(StartADCTask, NULL, &ADCTask_attributes);

  /* creation of ComTask */
  ComTaskHandle = osThreadNew(StartComTask, NULL, &ComTask_attributes);

  /* creation of OledTask */
  OledTaskHandle = osThreadNew(StartOledTask, NULL, &OledTask_attributes);

  /* creation of DHT11Task */
  DHT11TaskHandle = osThreadNew(StartDHT11Task, NULL, &DHT11Task_attributes);

  /* creation of SWCTask */
  SWCTaskHandle = osThreadNew(StartSWCTask, NULL, &SWCTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  AITaskHandle = osThreadNew(StartAITask, NULL, &AITask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 100;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */
  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */
  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */
  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */
  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */
  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_HARD_OUTPUT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */
  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */
  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */
  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 199;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 9999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */
  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */
  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */
  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 71;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */
  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */
  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */
  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */
  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */
  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */
  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 9600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */
  /* USER CODE END USART2_Init 2 */

}

/* ==== ESP32 新增：USART6 初始化（PC6=TX, PC7=RX, 115200, 8N1）==== */
/**
  * @brief USART6 Initialization Function (连接ESP32-S3)
  * @param None
  * @retval None
  */
static void MX_USART6_UART_Init(void)
{
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 115200;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(DHTPC1_GPIO_Port, DHTPC1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LD2_Pin|SWCPA7_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, BUZZER_Pin|DHT11_Pin|RES_Pin|DC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : DHTPC1_Pin */
  GPIO_InitStruct.Pin = DHTPC1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHTPC1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PA4_Pin */
  GPIO_InitStruct.Pin = PA4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PA4_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD2_Pin SWCPA7_Pin */
  GPIO_InitStruct.Pin = LD2_Pin|SWCPA7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : OB0_Pin */
  GPIO_InitStruct.Pin = OB0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(OB0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BUZZER_Pin RES_Pin DC_Pin */
  GPIO_InitStruct.Pin = BUZZER_Pin|RES_Pin|DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : DHT11_Pin */
  GPIO_InitStruct.Pin = DHT11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_InitStruct);

  /* ==== ESP32 新增：USART6 GPIO 配置（PC6=TX, PC7=RX）==== */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF8_USART6;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    if (rx_buf1[0] == '\r' || rx_buf1[0] == '\n')
    {
      if (cmd_len1 > 0)
      {
        cmd_buf1[cmd_len1] = '\0';
        parse_command(cmd_buf1, &huart1);
        cmd_len1 = 0;
        memset(cmd_buf1, 0, sizeof(cmd_buf1));
      }
    }
    else
    {
      if (cmd_len1 < 31) cmd_buf1[cmd_len1++] = rx_buf1[0];
    }
    HAL_UART_Receive_IT(&huart1, rx_buf1, 1);
  }

  else if (huart->Instance == USART2)
  {
    if (rx_buf2[0] == '\r' || rx_buf2[0] == '\n')
    {
      if (cmd_len2 > 0)
      {
        cmd_buf2[cmd_len2] = '\0';
        parse_command(cmd_buf2, &huart2);
        cmd_len2 = 0;
        memset(cmd_buf2, 0, sizeof(cmd_buf2));
      }
    }
    else
    {
      if (cmd_len2 < 31) cmd_buf2[cmd_len2++] = rx_buf2[0];
    }
    HAL_UART_Receive_IT(&huart2, rx_buf2, 1);
  }

  /* ==== ESP32 新增：USART6 接收回调占位（后续双向通信用）==== */
  else if (huart->Instance == USART6)
  {
      if (rx_buf6[0] == '\r' || rx_buf6[0] == '\n') {
          if (cmd_len6 > 0) {
              cmd_buf6[cmd_len6] = '\0';
              parse_command(cmd_buf6, &huart6);
              cmd_len6 = 0;
          }
      } else {
          if (cmd_len6 < 31) cmd_buf6[cmd_len6++] = rx_buf6[0];
      }
      HAL_UART_Receive_IT(&huart6, rx_buf6, 1);
  }
}

// ===== 自动模式下拒绝手动命令的快捷函数（ISR安全：只设标志，ComTask负责发送）=====
static void cmd_reject_manual(UART_HandleTypeDef *huart)
{
    sprintf(cmd_resp_text, "CMD_ERR: Need Manual Mode\r\n");
    cmd_resp_huart = huart;
    cmd_resp_pending = 1;
}

void parse_command(char *cmd, UART_HandleTypeDef *huart)
{
  /* ===== 模式切换：始终可用 ===== */
  if (strcmp(cmd, "ON") == 0)
  {
    manual_mode = 1;
  }
  else if (strcmp(cmd, "OFF") == 0)
  {
    manual_mode = 0;
    // 切回自动时复位所有外设，让自动Task接管
    // 低电平触发：SET(高)=关（风扇/补光灯/水泵统一）
    HAL_GPIO_WritePin(DHTPC1_GPIO_Port, DHTPC1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SWCPA7_GPIO_Port, SWCPA7_Pin, GPIO_PIN_SET);
    Servo_SetAngle(90);
    fan_state  = 0;
    bulb_state = 0;
    pump_state = 0;
    servo_angle = 90;
    buzzer_stop = 1;
    buzzer_mute = 0;
  }
  /* ===== BZOFF 紧急静音：始终可用 ===== */
  else if (strcmp(cmd, "BZOFF") == 0)
  {
    buzzer_stop = 1;
    buzzer_mute = 1;
  }
  /* ===== 以下命令仅在手动模式下可用 ===== */
  else if (strcmp(cmd, "AO") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    // 低电平触发：RESET(低)=风扇开
    HAL_GPIO_WritePin(DHTPC1_GPIO_Port, DHTPC1_Pin, GPIO_PIN_RESET);
    fan_state = 1;
  }
  else if (strcmp(cmd, "AF") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    // 低电平触发：SET(高)=风扇关
    HAL_GPIO_WritePin(DHTPC1_GPIO_Port, DHTPC1_Pin, GPIO_PIN_SET);
    fan_state = 0;
  }
  else if (strcmp(cmd, "BF") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    // 低电平触发：RESET(低)=补光灯关
    HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_RESET);
    bulb_state = 0;
  }
  else if (strcmp(cmd, "BO") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    // 低电平触发：SET(高)=补光灯开
    HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_SET);
    bulb_state = 1;
  }
  else if (strcmp(cmd, "CF") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    // 低电平触发：RESET(低)=水泵关
    HAL_GPIO_WritePin(SWCPA7_GPIO_Port, SWCPA7_Pin, GPIO_PIN_RESET);
    pump_state = 0;
  }
  else if (strcmp(cmd, "CO") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    // 低电平触发：SET(高)=水泵开
    HAL_GPIO_WritePin(SWCPA7_GPIO_Port, SWCPA7_Pin, GPIO_PIN_SET);
    pump_state = 1;
  }
  else if (strcmp(cmd, "SERVO_0") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    Servo_SetAngle(0);
    servo_angle = 0;
  }
  else if (strcmp(cmd, "SERVO_90") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    Servo_SetAngle(90);
    servo_angle = 90;
  }
  else if (strcmp(cmd, "SERVO_180") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    Servo_SetAngle(180);
    servo_angle = 180;
  }
  else if (strcmp(cmd, "BUZZER") == 0)
  {
    if (manual_mode == 0) { cmd_reject_manual(huart); return; }
    buzzer_trigger = 3;  // 中断里只设标志位，由AITask异步处理
  }
  /* ===== 未知命令 ===== */
  else
  {
    sprintf(cmd_resp_text, "CMD_ERR: Unknown\r\n");
    cmd_resp_huart = huart;
    cmd_resp_pending = 1;
    return;
  }
  /* ===== 成功响应（ISR安全：只设标志，ComTask负责实际发送）===== */
  sprintf(cmd_resp_text, "CMD_OK: %s\r\n", cmd);
  cmd_resp_huart = huart;
  cmd_resp_pending = 1;
}

int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

int fgetc(FILE *f)
{
    return EOF;  // IT中断模式已接管接收，阻塞式HAL_UART_Receive会与HAL_UART_Receive_IT冲突
}

uint8_t Key_Scan(void) {
  static uint8_t key_up = 1;
  if (key_up && (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_RESET)) {
    HAL_Delay(20);
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_RESET) {
      key_up = 0;
      return 1;
    }
  } else if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_SET) {
    key_up = 1;
  }
  return 0;
}

/* HAL_TIM_MspPostInit  PA8 引脚实现舵机*/
void HAL_TIM_MspPostInit(TIM_HandleTypeDef* htim)
{
  if (htim->Instance == TIM1) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  }
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartADCTask */
/**
  * @brief  实现ADCTask线程的函数（光照传感器采集 + 舵机控制）。
  * @param  argument: 未使用
  * @retval None
  */
/* USER CODE END Header_StartADCTask */
void StartADCTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  for(;;)
  {
    // 获取ADC互斥锁，防止与SWCTask冲突
    osSemaphoreAcquire(ADCBinarySemHandle, osWaitForever);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    if(HAL_IS_BIT_SET(HAL_ADC_GetState(&hadc1), HAL_ADC_STATE_REG_EOC))
    {
      ADC_VALUE = HAL_ADC_GetValue(&hadc1);
      voltage = ADC_VALUE * 3.3f / 4095.0f;

      light_intensity = (REFERENCE_VOLTAGE - voltage) * CONVERSION_FACTOR;
      if(light_intensity < 0) light_intensity = 0;

      if (manual_mode == 0)
      {
        // 低电平触发：RESET(低)=开 / SET(高)=关
        if (voltage < LIGHT_THRESHOLD) {
          HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_RESET);
          bulb_state = 1;
        } else {
          HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_SET);
          bulb_state = 0;
        }

        // 舵机自动控制：根据光照强度分段调整角度（模拟遮阳帘）
        if (light_intensity >= SERVO_THRESHOLD_2) {
          Servo_SetAngle(180);   // 强光 → 全关遮阳
          servo_angle = 180;
        } else if (light_intensity >= SERVO_THRESHOLD_1) {
          Servo_SetAngle(90);    // 中等光照 → 半开
          servo_angle = 90;
        } else {
          Servo_SetAngle(0);     // 弱光 → 全开采光
          servo_angle = 0;
        }
      }
    }
    HAL_ADC_Stop(&hadc1);  // 释放ADC，让SWCTask可以切换通道

    osSemaphoreRelease(ADCBinarySemHandle);  // 释放ADC互斥锁
    osSemaphoreRelease(ADCCountingSemHandle);
    osDelay(1000);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartComTask */
/**
* @brief 实现ComTask线程的函数（串口数据推送：USART1蓝牙 + USART2电脑 + USART6 ESP32）。
* @param argument: 未使用
* @retval None
*/
/* USER CODE END Header_StartComTask */
void StartComTask(void *argument)
{
  /* USER CODE BEGIN StartComTask */
  char send_buf[128];
  char esp32_buf[128];  /* ==== ESP32 新增：JSON格式发送缓冲区 ==== */
  for(;;)
  {
    // 优先处理来自中断的命令回复（ISR安全延迟发送机制）
    if (cmd_resp_pending) {
      UART_HandleTypeDef *target = cmd_resp_huart;
      osMutexAcquire(UART_MutexHandle, osWaitForever);
      HAL_UART_Transmit(target, (uint8_t*)cmd_resp_text, strlen(cmd_resp_text), HAL_MAX_DELAY);
      osMutexRelease(UART_MutexHandle);
      cmd_resp_pending = 0;
    }

    osSemaphoreAcquire(DHT11CountingSemHandle, osWaitForever);
    osSemaphoreAcquire(ADCCountingSemHandle, osWaitForever);
    osSemaphoreAcquire(SWCCountingSemHandle, osWaitForever);

    /* ---- 原有：USART1(蓝牙) + USART2(电脑) 纯文本格式 ---- */
    sprintf(send_buf, "Temp:%.1fC\r\nHumi:%.1f%%\r\nSoil:%d%%\r\nLight:%.0flux\r\nAI:%s\r\n",
            dht11_temp, dht11_humi, soil_humidity, light_intensity,
            ai_status == 0 ? "Healthy" :
            ai_status == 1 ? "LIGHT!" :
            ai_status == 2 ? "TEMP!" : "WATER!");

    osMutexAcquire(UART_MutexHandle, osWaitForever);
    HAL_UART_Transmit(&huart1, (uint8_t*)send_buf, strlen(send_buf), HAL_MAX_DELAY);
    osMutexRelease(UART_MutexHandle);

    osMutexAcquire(UART_MutexHandle, osWaitForever);
    HAL_UART_Transmit(&huart2, (uint8_t*)send_buf, strlen(send_buf), HAL_MAX_DELAY);
    osMutexRelease(UART_MutexHandle);

    /* ==== ESP32 新增：USART6 JSON格式发送（给ESP32-S3 → WiFi → 云端）==== */
    sprintf(esp32_buf,
        "{\"temp\":%.1f,\"humi\":%.1f,\"soil\":%d,\"light\":%.0f,\"ai\":%d,"
        "\"fan\":%d,\"bulb\":%d,\"pump\":%d,\"servo\":%d,\"mode\":%d}\r\n",
        dht11_temp, dht11_humi, soil_humidity, light_intensity, ai_status,
        fan_state, bulb_state, pump_state, servo_angle, manual_mode);

    osMutexAcquire(UART_MutexHandle, osWaitForever);
    HAL_UART_Transmit(&huart6, (uint8_t*)esp32_buf, strlen(esp32_buf), HAL_MAX_DELAY);
    osMutexRelease(UART_MutexHandle);

    osSemaphoreRelease(SWCCountingSemHandle);
    osSemaphoreRelease(ADCCountingSemHandle);
    osSemaphoreRelease(DHT11CountingSemHandle);

    osDelay(2000);
  }
  /* USER CODE END StartComTask */
}

/* USER CODE BEGIN Header_StartOledTask */
/**
* @brief 实现OledTask线程的函数（OLED显示刷新）。
* @param argument: 未使用
* @retval None
*/
/* USER CODE END Header_StartOledTask */
void StartOledTask(void *argument)
{
  /* USER CODE BEGIN StartOledTask */
  OLED_Clear();
  for(;;)
  {
    if (Key_Scan()) {
      display_mode = (display_mode + 1) % 4;
      OLED_Clear();
    }

    if (display_mode == 0) {
      osSemaphoreAcquire(DHT11CountingSemHandle, osWaitForever);

      OLED_ShowString(0, 0, "Temp:", OLED_8X16);
      if (dht11_read_status == 0) {
        OLED_ShowSignedNum(48, 0, (int32_t)(dht11_temp * 10), 3, OLED_8X16);
        OLED_ShowChar(72, 0, '.', OLED_8X16);
        OLED_ShowSignedNum(80, 0, (int32_t)(dht11_temp * 10) % 10, 1, OLED_8X16);
        OLED_ShowChar(88, 0, 'C', OLED_8X16);
      } else {
        OLED_ShowString(48, 0, "Err", OLED_8X16);
      }
      OLED_ShowString(0, 16, "Humi:", OLED_8X16);
      if (dht11_read_status == 0) {
        OLED_ShowSignedNum(48, 16, (int32_t)(dht11_humi * 10), 3, OLED_8X16);
        OLED_ShowChar(72, 16, '.', OLED_8X16);
        OLED_ShowSignedNum(80, 16, (int32_t)(dht11_humi * 10) % 10, 1, OLED_8X16);
        OLED_ShowChar(88, 16, '%', OLED_8X16);
      } else {
        OLED_ShowString(48, 16, "Err", OLED_8X16);
      }

      osSemaphoreRelease(DHT11CountingSemHandle);
    }

    else if (display_mode == 2) {
      osSemaphoreAcquire(SWCCountingSemHandle, osWaitForever);

      OLED_ShowString(0, 0, "Soil Volt:", OLED_8X16);
      sprintf(str3, " %.2fV", soil_voltage);
      OLED_ShowString(64, 0, str3, OLED_8X16);
      OLED_ShowString(0, 16, "Humidity:", OLED_8X16);
      sprintf(str3, " %d%%", soil_humidity);
      OLED_ShowString(64, 16, str3, OLED_8X16);

      osSemaphoreRelease(SWCCountingSemHandle);
    }

  else if (display_mode == 3) {
      const char* ai_str;
      if (ai_status == 0)      ai_str = "Healthy";
      else if (ai_status == 1) ai_str = "LIGHT!";
      else if (ai_status == 2) ai_str = "TEMP!";
      else                     ai_str = "WATER!";

      OLED_ShowString(0, 0, "AI Status:", OLED_8X16);
      OLED_ShowString(0, 24, (char*)ai_str, OLED_8X16);
  }

    else {
      osSemaphoreAcquire(ADCCountingSemHandle, osWaitForever);
      OLED_Clear();

      OLED_ShowString(0, 0, "Voltage:", OLED_8X16);
      OLED_ShowString(0, 24, "Light:", OLED_8X16);

      sprintf(str1, "%.2fV", voltage);
      OLED_ShowString(64, 0, str1, OLED_8X16);

      sprintf(str2, "%.0flux", light_intensity);
      OLED_ShowString(64, 24, str2, OLED_8X16);

      osSemaphoreRelease(ADCCountingSemHandle);
    }

    OLED_Update();
    osDelay(1);
  }
  /* USER CODE END StartOledTask */
}

/* USER CODE BEGIN Header_StartDHT11Task */
/**
* @brief 实现DHT11Task线程的函数（温湿度采集 + 风扇控制）。
* @param argument: 未使用
* @retval None
*/
/* USER CODE END Header_StartDHT11Task */
void StartDHT11Task(void *argument)
{
  /* USER CODE BEGIN StartDHT11Task */
  for(;;)
  {
    dht11_read_status = DHT11_Read_Data(&dht11_temp, &dht11_humi);
    float u = dht11_temp;

    if (manual_mode == 0)
    {
      if (u >= TEMP_THRESHOLD) {
        // 温度达标：低电平触发 → RESET(低)=开
        HAL_GPIO_WritePin(DHTPC1_GPIO_Port, DHTPC1_Pin, GPIO_PIN_RESET);
        fan_state = 1;
      } else {
        // 温度不达标：低电平触发 → SET(高)=关
        HAL_GPIO_WritePin(DHTPC1_GPIO_Port, DHTPC1_Pin, GPIO_PIN_SET);
        fan_state = 0;
      }
    }

    osSemaphoreRelease(DHT11CountingSemHandle);
    osDelay(2000);
  }
  /* USER CODE END StartDHT11Task */
}

/* USER CODE BEGIN Header_StartSWCTask */
/**
* @brief 实现SWCTask线程的函数（土壤湿度采集 + 水泵控制）。
* @param argument: 未使用
* @retval None
*/
/* USER CODE END Header_StartSWCTask */
void StartSWCTask(void *argument)
{
  /* USER CODE BEGIN StartSWCTask */
  for(;;)
  {
    // 获取ADC互斥锁，防止与ADCTask冲突
    osSemaphoreAcquire(ADCBinarySemHandle, osWaitForever);

    HAL_ADC_Start(&hadc1);
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_6;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    HAL_ADC_PollForConversion(&hadc1, 10);
    if(HAL_IS_BIT_SET(HAL_ADC_GetState(&hadc1), HAL_ADC_STATE_REG_EOC))
    {
      uint32_t adc_value = HAL_ADC_GetValue(&hadc1);
      soil_voltage = adc_value * 3.3f / 4095.0f;

      if(soil_voltage >= SOIL_HUMIDITY_MAX)
        soil_humidity = 0;
      else if(soil_voltage <= SOIL_HUMIDITY_MIN)
        soil_humidity = 100;
      else
        soil_humidity = 100 - (uint8_t)((soil_voltage / SOIL_HUMIDITY_MAX) * 100);

      if (manual_mode == 0)
      {
        // 低电平触发：RESET(低)=开 / SET(高)=关
        if (soil_humidity < SOIL_HUMIDITY_THRESHOLD) {
          HAL_GPIO_WritePin(SWCPA7_GPIO_Port, SWCPA7_Pin, GPIO_PIN_RESET);
          pump_state = 1;
        } else {
          HAL_GPIO_WritePin(SWCPA7_GPIO_Port, SWCPA7_Pin, GPIO_PIN_SET);
          pump_state = 0;
        }
      }
    }
    HAL_ADC_Stop(&hadc1);

    // 恢复ADC通道为CH0（ADCTask默认使用的光照通道）
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = 1;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    osSemaphoreRelease(ADCBinarySemHandle);  // 释放ADC互斥锁
    osSemaphoreRelease(SWCCountingSemHandle);
    osDelay(1000);
  }
  /* USER CODE END StartSWCTask */
}

/* USER CODE BEGIN StartAITask */
/**
  * @brief AI推理 + 蜂鸣器异步处理任务。
  *        蜂鸣器使用状态机方式，避免阻塞中断或阻塞任务调度。
  *        支持手动关闭蜂鸣器（BZOFF指令），关闭后静音30秒防止AI自动重触发。
  * @param argument: 未使用
  * @retval None
  */
void StartAITask(void *argument)
{
    // 蜂鸣器状态机变量
    uint8_t  buzzer_count      = 0;    // 剩余需要响的次数
    uint8_t  buzzer_phase      = 0;    // 0=空闲 1=正在响 2=等待间隔
    uint32_t buzzer_next_tick  = 0;    // 下一次动作的Tick时间
    uint32_t last_ai_tick      = 0;    // 上一次AI推理的Tick时间
    uint32_t buzzer_mute_tick  = 0;    // 静音开始时间（ms）

    for (;;)
    {
        uint32_t now = HAL_GetTick();

        // ===== 手动关闭蜂鸣器（BZOFF指令，最高优先级检测）=====
        if (buzzer_stop) {
            Buzzer_Off();               // 立即关闭蜂鸣器硬件
            buzzer_count      = 0;      // 清零剩余次数
            buzzer_phase      = 0;      // 回到空闲状态
            buzzer_trigger    = 0;      // 清除可能残留的触发
            buzzer_stop       = 0;      // 清除停止标志
            buzzer_mute_tick  = now;    // 记录静音开始时间
        }

        // ===== 静音超时自动解除（30秒后或AI状态恢复健康时）=====
        if (buzzer_mute) {
            // 条件1：状态恢复正常 → 立即解除静音
            // 条件2：超过30秒 → 超时解除，允许AI再次报警
            if (ai_status == 0 || (now - buzzer_mute_tick > 30000)) {
                buzzer_mute = 0;
            }
        }

        // ===== 蜂鸣器异步状态机 =====
        // 检查是否有新的触发请求
        if (buzzer_trigger > 0 && buzzer_phase == 0) {
            buzzer_count = buzzer_trigger;
            buzzer_trigger = 0;
            buzzer_phase = 1;           // 进入"正在响"阶段
            Buzzer_On();                // RESET(低电平)→蜂鸣器响
            buzzer_next_tick = now + 150;  // 150ms后关
        }

        // 状态1：正在响，等待150ms后关闭
        if (buzzer_phase == 1 && now >= buzzer_next_tick) {
            Buzzer_Off();               // SET(高电平)→蜂鸣器停
            buzzer_count--;
            if (buzzer_count > 0) {
                buzzer_phase = 2;           // 进入"等待间隔"阶段
                buzzer_next_tick = now + 300; // 间隔300ms
            } else {
                buzzer_phase = 0;           // 全部完成，回到空闲
            }
        }

        // 状态2：等待间隔，到达后开始下一声
        if (buzzer_phase == 2 && now >= buzzer_next_tick) {
            Buzzer_On();                // RESET(低电平)→蜂鸣器响
            buzzer_phase = 1;
            buzzer_next_tick = now + 150;
        }

        // ===== AI推理（每2秒执行一次）=====
        if (now - last_ai_tick >= 2000 || last_ai_tick == 0) {
            float temp_val  = dht11_temp;
            float humi_val  = dht11_humi;
            float soil_val  = (float)soil_humidity;
            float light_val = light_intensity;

            ai_status = (uint8_t)AI_Predict(temp_val, humi_val, soil_val, light_val);

            // 异常时自动触发蜂鸣器（静音模式下跳过）
            if (ai_status != 0 && buzzer_phase == 0 && buzzer_mute == 0) {
                buzzer_trigger = 3;
            }

            last_ai_tick = now;
        }

        osDelay(50);  // 每50ms轮询一次，保证蜂鸣器时序精确
    }
}
/* USER CODE END StartAITask */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM10 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */
  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM10) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
