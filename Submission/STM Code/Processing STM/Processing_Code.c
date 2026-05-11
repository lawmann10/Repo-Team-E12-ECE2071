/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stdlib.h"
#include "string.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* ===========================================================================
   OPERATING MODES
   Controlled by 2-byte UART commands from the PC: [mode_byte, param_byte]

   MODE_IDLE      (-1): not recording, ignoring SPI data
   MODE_TIMED      (0): record for a fixed duration (param = seconds)
   MODE_DIST_NEAR  (1): distance trigger - object in range, send real audio
   MODE_DIST_FAR   (2): distance trigger - object out of range, send silence
   MODE_DIST_INIT  (3): distance trigger - waiting for first ultrasonic reading
   =========================================================================== */
#define MODE_IDLE       -1
#define MODE_TIMED       0
#define MODE_DIST_NEAR   1
#define MODE_DIST_FAR    2
#define MODE_DIST_INIT   3

/* ===========================================================================
   AUDIO BUFFER SIZES
   Each buffer holds 1024 samples (uint16_t) packed as big-endian byte pairs,
   so 2048 bytes per buffer. While one buffer is being sent over UART DMA,
   the other fills up with new SPI samples (ping-pong buffering).
   =========================================================================== */
#define SAMPLES_PER_BUFFER  1024
#define BYTES_PER_BUFFER    (SAMPLES_PER_BUFFER * 2)   // 2048 bytes

/* ===========================================================================
   AUDIO FILTERING CONSTANTS
   - OUTLIER_THRESHOLD: ADC is 12-bit (max 4095) but values above 4025
     are treated as noise spikes and replaced with the last good value
   - SILENCE_VALUE: mid-rail value representing no audio signal (1985)
   =========================================================================== */
#define OUTLIER_THRESHOLD   4025
#define SILENCE_VALUE       1985

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef  hspi1;
DMA_HandleTypeDef  hdma_spi1_rx;

TIM_HandleTypeDef  htim1;
TIM_HandleTypeDef  htim6;
TIM_HandleTypeDef  htim16;

UART_HandleTypeDef huart2;
DMA_HandleTypeDef  hdma_usart2_tx;

/* USER CODE BEGIN PV */

volatile int mode = MODE_IDLE;

float desired_duration_s  = 0;   // timed mode: stop after this many seconds
int   desired_distance_cm = 0;   // distance trigger: record when closer than this

/* Ping-pong audio buffers */
uint8_t buffer_1[BYTES_PER_BUFFER];
uint8_t buffer_2[BYTES_PER_BUFFER];

volatile int     sample_idx = 0;    // current write position (0-2047), wraps around
volatile uint8_t send_buf_1 = 0;    // set to 1 when buffer 1 is full and ready to send
volatile uint8_t send_buf_2 = 0;    // set to 1 when buffer 2 is full and ready to send

int sent_packets = 0;               // how many buffers we've sent this recording

/* Audio filtering state */
uint16_t received_value       = 0;               // latest SPI sample (filled by DMA)
uint16_t previous_valid_value = SILENCE_VALUE;   // fallback for outlier rejection

uint16_t avg_buffer[2] = {SILENCE_VALUE, SILENCE_VALUE};
uint32_t avg_sum       = SILENCE_VALUE * 2;
uint16_t avg_idx       = 0;

/* command_bytes[0] = mode to enter, command_bytes[1] = parameter (duration or distance) */
uint8_t command_bytes[2];

/* Ultrasonic sensor state */
volatile uint16_t echo_rise  = 0;
volatile uint16_t echo_fall  = 0;
volatile uint16_t echo_width = 0;
volatile uint8_t  echo_ready = 0;   // set to 1 when a full echo has been received
volatile int      pulse_sent = 0;   // 1 while waiting for an echo to come back
uint32_t          last_pulse_time = 0;
uint8_t           debounce_count  = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM16_Init(void);
static void MX_TIM6_Init(void);
static void MX_TIM1_Init(void);
static void MX_SPI1_Init(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* 2-sample moving average to smooth noise between consecutive samples */
uint16_t moving_average(uint16_t new_sample)
{
    avg_sum -= avg_buffer[avg_idx];
    avg_buffer[avg_idx] = new_sample;
    avg_sum += new_sample;
    avg_idx = (avg_idx + 1) % 2;
    return (uint16_t)(avg_sum / 2);
}

/* Write one 16-bit sample (big-endian) into whichever buffer is currently
   filling. Flags the buffer as ready-to-send when it becomes full. */
static void write_sample_to_buffer(uint16_t value)
{
    uint8_t hi = (value >> 8) & 0xFF;
    uint8_t lo = value & 0xFF;

    if (sample_idx < SAMPLES_PER_BUFFER) {
        uint16_t i = sample_idx * 2;
        buffer_1[i]     = hi;
        buffer_1[i + 1] = lo;
        if (sample_idx == SAMPLES_PER_BUFFER - 1)
            send_buf_1 = 1;
    } else {
        uint16_t i = (sample_idx - SAMPLES_PER_BUFFER) * 2;
        buffer_2[i]     = hi;
        buffer_2[i + 1] = lo;
        if (sample_idx == SAMPLES_PER_BUFFER * 2 - 1)
            send_buf_2 = 1;
    }

    sample_idx++;
    if (sample_idx >= SAMPLES_PER_BUFFER * 2)
        sample_idx = 0;
}

/* Apply debounce before switching distance-trigger modes. We require 3
   consecutive readings pointing to the same target before actually switching.
   On the very first reading (MODE_DIST_INIT), skip debounce and switch immediately. */
static void apply_distance_debounce(uint8_t target_mode)
{
    if ((int)target_mode == mode) {
        debounce_count = 0;
        return;
    }

    debounce_count++;

    if (debounce_count >= 3 || mode == MODE_DIST_INIT) {
        mode = target_mode;
        HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, (mode == MODE_DIST_NEAR));
        debounce_count = 0;
    }
}

/* TIM1 input capture callback: records rising and falling edge times of the
   ultrasonic echo pulse to calculate echo width (proportional to distance) */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM1) return;

    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        echo_rise = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
    }
    else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
        echo_fall  = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
        echo_width = (uint16_t)(echo_fall - echo_rise);
        echo_ready = 1;
    }
}

/* Manages the ultrasonic sensor: sends 10us trigger pulses every 60ms and
   processes echoes to determine whether the object is in range.
   Call this in the main loop whenever we are in distance-trigger mode. */
void ultrasonic_manager(void)
{
    /* Process echo if one has arrived */
    if (echo_ready) {
        float distance_cm = (echo_width * 0.0343f) / 2.0f;
        echo_ready = 0;
        pulse_sent = 0;
        __HAL_TIM_SET_COUNTER(&htim6, 0);  // reset spacing timer for next pulse

        uint8_t target = (desired_distance_cm >= distance_cm) ? MODE_DIST_NEAR : MODE_DIST_FAR;
        apply_distance_debounce(target);
    }

    /* Timeout: if echo never came back within 100ms, assume object is out of range */
    if (pulse_sent && (HAL_GetTick() - last_pulse_time > 100)) {
        pulse_sent = 0;
        __HAL_TIM_SET_COUNTER(&htim6, 0);
        apply_distance_debounce(MODE_DIST_FAR);
    }

    /* Send a 10us trigger pulse once the 60ms spacing timer has elapsed */
    if (!pulse_sent && __HAL_TIM_GET_COUNTER(&htim6) > 60000) {
        HAL_GPIO_WritePin(Trigger_GPIO_Port, Trigger_Pin, 1);
        __HAL_TIM_SET_COUNTER(&htim16, 0);
        pulse_sent      = 1;
        last_pulse_time = HAL_GetTick();
    }

    /* End trigger pulse after 10us */
    if (pulse_sent && __HAL_TIM_GET_COUNTER(&htim16) > 10) {
        HAL_GPIO_WritePin(Trigger_GPIO_Port, Trigger_Pin, 0);
    }
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
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_TIM16_Init();
  MX_TIM6_Init();
  MX_TIM1_Init();
  MX_SPI1_Init();

  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  /* Start timers used by the ultrasonic sensor */
  HAL_TIM_Base_Start(&htim16);
  HAL_TIM_Base_Start(&htim6);

  /* Start TIM1 input capture interrupts for ultrasonic echo detection */
  HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_1);
  HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_2);

  /* Start listening for SPI audio samples via DMA */
  HAL_SPI_Receive_DMA(&hspi1, (uint8_t*)&received_value, 1);

  /* Start listening for 2-byte commands from the PC via UART interrupt */
  HAL_UART_Receive_IT(&huart2, command_bytes, 2);

  /* Enable UART TX DMA (needed for DMA-based audio transmission to PC) */
  huart2.Instance->CR3 |= USART_CR3_DMAT;

  float recording_duration;

  while (1) {

    /* Run ultrasonic manager when in distance-trigger mode */
    if (mode >= MODE_DIST_NEAR) {
        ultrasonic_manager();
    }

    /* Transmit any full audio buffers to the PC */
    if (mode >= MODE_TIMED) {

        if (send_buf_1) {
            send_buf_1 = 0;
            sent_packets++;
            HAL_UART_Transmit_DMA(&huart2, buffer_1, BYTES_PER_BUFFER);
        }
        else if (send_buf_2) {
            send_buf_2 = 0;
            sent_packets++;
            HAL_UART_Transmit_DMA(&huart2, buffer_2, BYTES_PER_BUFFER);
        }

        /* In timed mode, stop once the requested duration is reached.
           Each packet takes ~23.2ms to transmit (2048 bytes at 921600 baud). */
        if (mode == MODE_TIMED) {
            recording_duration = sent_packets * 0.0232199546f;
            if (recording_duration >= desired_duration_s) {
                mode = MODE_IDLE;
                HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 0);
            }
        }
    }

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
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState            = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange       = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM           = 1;
  RCC_OscInitStruct.PLL.PLLN           = 16;
  RCC_OscInitStruct.PLL.PLLP           = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ           = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR           = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */

  /* SPI1 parameter configuration*/
  hspi1.Instance               = SPI1;
  hspi1.Init.Mode              = SPI_MODE_SLAVE;
  hspi1.Init.Direction         = SPI_DIRECTION_2LINES_RXONLY;
  hspi1.Init.DataSize          = SPI_DATASIZE_16BIT;
  hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
  hspi1.Init.NSS               = SPI_NSS_SOFT;
  hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial     = 7;
  hspi1.Init.CRCLength         = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

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

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef      sConfigIC     = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */

  htim1.Instance               = TIM1;
  htim1.Init.Prescaler         = 31;
  htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim1.Init.Period            = 65535;
  htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_IC_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger  = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode      = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* CH1: capture rising edge (echo starts) */
  sConfigIC.ICPolarity  = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter    = 0;
  if (HAL_TIM_IC_ConfigChannel(&htim1, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /* CH2: capture falling edge (echo ends) */
  sConfigIC.ICPolarity  = TIM_INPUTCHANNELPOLARITY_FALLING;
  sConfigIC.ICSelection = TIM_ICSELECTION_INDIRECTTI;
  if (HAL_TIM_IC_ConfigChannel(&htim1, &sConfigIC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */

  htim6.Instance               = TIM6;
  htim6.Init.Prescaler         = 31;
  htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim6.Init.Period            = 65535;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief TIM16 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM16_Init(void)
{

  /* USER CODE BEGIN TIM16_Init 0 */

  /* USER CODE END TIM16_Init 0 */

  /* USER CODE BEGIN TIM16_Init 1 */

  /* USER CODE END TIM16_Init 1 */

  htim16.Instance               = TIM16;
  htim16.Init.Prescaler         = 31;
  htim16.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim16.Init.Period            = 65535;
  htim16.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN TIM16_Init 2 */

  /* USER CODE END TIM16_Init 2 */

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

  huart2.Instance                    = USART2;
  huart2.Init.BaudRate               = 921600;
  huart2.Init.WordLength             = UART_WORDLENGTH_8B;
  huart2.Init.StopBits               = UART_STOPBITS_1;
  huart2.Init.Parity                 = UART_PARITY_NONE;
  huart2.Init.Mode                   = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling           = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, Trigger_Pin | Debug0_Pin | Debug1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : Trigger_Pin Debug0_Pin Debug1_Pin */
  GPIO_InitStruct.Pin   = Trigger_Pin | Debug0_Pin | Debug1_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LD3_Pin */
  GPIO_InitStruct.Pin   = LD3_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* SPI RX complete: called by DMA each time a new 16-bit audio sample arrives.
   Filters the sample and writes it into whichever buffer is currently filling. */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1) return;

    if (mode <= MODE_DIST_NEAR) {  // MODE_IDLE(-1), MODE_TIMED(0), MODE_DIST_NEAR(1)
        /* Filter: reject obvious noise spikes, then smooth with moving average */
        uint16_t filtered;
        if (received_value > OUTLIER_THRESHOLD) {
            filtered = previous_valid_value;  // replace spike with last good value
        } else {
            filtered = moving_average(received_value);
            previous_valid_value = filtered;
        }
        write_sample_to_buffer(filtered);
    }
    else if (mode == MODE_DIST_FAR) {
        /* Object out of range: send silence so the PC receives continuous data */
        write_sample_to_buffer(SILENCE_VALUE);
    }
    /* MODE_DIST_INIT(3): ultrasonic sensor hasn't read yet, do nothing */
}

/* UART RX complete: called when a 2-byte command arrives from the PC.
   Resets all recording state before applying the new mode. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) return;

    /* Reset state so a fresh recording starts cleanly */
    sent_packets = 0;
    sample_idx   = 0;
    send_buf_1   = 0;
    send_buf_2   = 0;

    avg_buffer[0] = SILENCE_VALUE;
    avg_buffer[1] = SILENCE_VALUE;
    avg_sum       = SILENCE_VALUE * 2;
    avg_idx       = 0;

    uint8_t cmd   = command_bytes[0];
    uint8_t param = command_bytes[1];

    if (cmd == 0) {
        /* Timed recording: record for 'param' seconds */
        desired_duration_s = param;
        mode = MODE_TIMED;
        HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 1);
    }
    else if (cmd == 1) {
        /* Distance trigger: start in DIST_INIT so we wait for the first reading */
        desired_distance_cm = param;
        mode = MODE_DIST_INIT;
    }
    else if (cmd == 2) {
        /* Stop recording */
        mode = MODE_IDLE;
        HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 0);
    }

    /* Re-arm the interrupt for the next command */
    HAL_UART_Receive_IT(huart, command_bytes, 2);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    HAL_GPIO_WritePin(Debug1_GPIO_Port, Debug1_Pin, 0);
}

/* UART error recovery: clear flags and re-arm the receive interrupt */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) return;

    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);

    HAL_UART_Receive_IT(huart, command_bytes, 2);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
