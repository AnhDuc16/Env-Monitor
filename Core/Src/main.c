#include "main.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "mk_dht11.h"

ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim1;
UART_HandleTypeDef huart1;

typedef struct {
    float temperature;
    float humidity;
    uint32_t gas_value;
} EnvData_t;

EnvData_t g_env_data;
osMutexId_t myMutex01Handle;

dht11_t dht11;
char strBuffer[128];
uint32_t GAS_THRESHOLD = 2500;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);

// Hàm đọc ADC
uint32_t Read_MQ2(void) {
    uint32_t val = 0;
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 50) == HAL_OK) {
        val = HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    return val;
}

// Task 1: Đọc cảm biến (DHT11 + MQ2)
void Task_Sensor(void *argument) {
    EnvData_t local_data;
    for(;;) {
        readDHT11(&dht11);
        local_data.temperature = (float)dht11.temperature;
        local_data.humidity = (float)dht11.humidty;
        local_data.gas_value = Read_MQ2();

        // Cập nhật biến toàn cục (có Mutex bảo vệ)
        if (osMutexAcquire(myMutex01Handle, osWaitForever) == osOK) {
            g_env_data = local_data;
            osMutexRelease(myMutex01Handle);
        }

        osDelay(2000); // Đọc mỗi 2 giây
    }
}

// Task 2: Hiển thị OLED
void Task_Display(void *argument) {
    EnvData_t current_data;
    for(;;) {
        // Lấy dữ liệu mới nhất
        if (osMutexAcquire(myMutex01Handle, osWaitForever) == osOK) {
            current_data = g_env_data;
            osMutexRelease(myMutex01Handle);
        }

        if (osMutexAcquire(myMutex01Handle, osWaitForever) == osOK) {
            ssd1306_Fill(Black);

            if (current_data.temperature == 0 && current_data.humidity == 0) {
                ssd1306_SetCursor(0, 10);
                ssd1306_WriteString("Temp: -- C  ", Font_7x10, White);
                ssd1306_SetCursor(0, 30);
                ssd1306_WriteString("Humi: -- %  ", Font_7x10, White);
            } else {
                sprintf(strBuffer, "Temp: %d C  ", (int)current_data.temperature);
                ssd1306_SetCursor(0, 10);
                ssd1306_WriteString(strBuffer, Font_7x10, White);
                sprintf(strBuffer, "Humi: %d %%  ", (int)current_data.humidity);
                ssd1306_SetCursor(0, 30);
                ssd1306_WriteString(strBuffer, Font_7x10, White);
            }

            sprintf(strBuffer, "Gas: %lu     ", current_data.gas_value);
            ssd1306_SetCursor(0, 50);
            ssd1306_WriteString(strBuffer, Font_7x10, White);
            ssd1306_UpdateScreen();

            osMutexRelease(myMutex01Handle);
        }

        // Điều khiển LED
        if (current_data.gas_value > GAS_THRESHOLD) {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);   // Đỏ sáng
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
        } else {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);   // Xanh sáng
        }

        osDelay(500); // Cập nhật màn hình mỗi 0.5s
    }
}

// Task 3: Gửi UART
void Task_Comm(void *argument) {
    EnvData_t current_data;
    for(;;) {
        // Lấy dữ liệu mới nhất
        if (osMutexAcquire(myMutex01Handle, osWaitForever) == osOK) {
            current_data = g_env_data;
            osMutexRelease(myMutex01Handle);
        }

        // Gửi JSON
        // Gửi JSON
        sprintf(strBuffer, "{\"temp\":%d,\"humi\":%d,\"gas\":%lu}\r\n",
                (int)current_data.temperature, (int)current_data.humidity, current_data.gas_value);

        // Thêm timeout 1000 (1 giây) thay vì 100 (100ms)
        HAL_UART_Transmit(&huart1, (uint8_t*)strBuffer, strlen(strBuffer), 1000);
        osDelay(1000); // Gửi PC mỗi 1 giây
    }
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();

  __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_TC | UART_FLAG_RXNE);

  // Khởi tạo ngoại vi
  ssd1306_Init();
  init_dht11(&dht11, &htim1, GPIOA, GPIO_PIN_1);

  // Bắt đầu Timer cho DHT11
  HAL_TIM_Base_Start(&htim1);

  // Tắt LED và Còi ban đầu
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  // Khởi tạo giá trị ban đầu cho biến toàn cục
  g_env_data.temperature = 0;
  g_env_data.humidity = 0;
  g_env_data.gas_value = 0;

  osKernelInitialize();

  myMutex01Handle = osMutexNew(NULL);

  // Tạo các Task
  const osThreadAttr_t sensor_attr = {.name = "Sensor", .stack_size = 256 * 4, .priority = osPriorityHigh};
  const osThreadAttr_t display_attr = {.name = "Display", .stack_size = 512 * 4, .priority = osPriorityNormal};
  const osThreadAttr_t comm_attr = {.name = "Comm", .stack_size = 256 * 4, .priority = osPriorityLow};

  osThreadNew(Task_Sensor, NULL, &sensor_attr);
  osThreadNew(Task_Display, NULL, &display_attr);
  osThreadNew(Task_Comm, NULL, &comm_attr);

  osKernelStart();

  while (1) { }
}

/* ================= CÁC HÀM KHỞI TẠO ================= */

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) { Error_Handler(); }
}

static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }
}

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM1_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 71;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK) { Error_Handler(); }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK) { Error_Handler(); }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK) { Error_Handler(); }
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) { Error_Handler(); }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2|GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}
