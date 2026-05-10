/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body (Processing STM - Task 2, Working Architecture)
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Peripherals */
SPI_HandleTypeDef hspi1;        // SPI from Sampling STM (replaces huart1)
TIM_HandleTypeDef htim2;        // Periodic timer for ultrasonic trigger ISR
TIM_HandleTypeDef htim16;       // Microsecond timer for echo measurement
UART_HandleTypeDef huart2;      // UART to PC (Python)

/* USER CODE BEGIN PV */

/* Mode and recording state */
char mode = 0;
uint8_t is_recording = 0;
uint8_t prev_detected = 0;
uint8_t object_detected = 0;

/* Configurable trigger distance sent from Python e.g. "D10\n" → 10 */
uint16_t user_distance = 10;

/* Moving average filter state */
uint8_t prev_sample = 0;

/* Ultrasonic ISR flag — set by TIM2 ISR, read in main loop */
volatile uint8_t ultrasonic_trigger_flag = 0;

/* Newline-terminated command parsing */
#define RX_BUFFER_SIZE 10
char rx_buffer[RX_BUFFER_SIZE];
uint8_t rx_index = 0;

/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM16_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);

/* USER CODE BEGIN 0 */

/* Microsecond delay using htim16 */
void delay_us(uint16_t us) {
    __HAL_TIM_SET_COUNTER(&htim16, 0);
    while (__HAL_TIM_GET_COUNTER(&htim16) < us);
}

/*
 * Parse a newline-terminated command from Python.
 * "M\n"    → manual mode
 * "D10\n"  → distance mode, trigger at 10cm
 * "S\n"    → stop
 */
void process_command(char *cmd)
{
    if (cmd[0] == 'M') {
        mode = 'M';
        is_recording = 0;
        prev_sample = 0;
        object_detected = 0;
        prev_detected = 0;

        /* Send ready byte so Python starts its recording timer */
        uint8_t ready = 'R';
        HAL_UART_Transmit(&huart2, &ready, 1, HAL_MAX_DELAY);

    } else if (cmd[0] == 'D') {
        mode = 'D';
        /* Parse distance value from command string e.g. "D10" → 10 */
        user_distance = (uint16_t)atoi(&cmd[1]);
        if (user_distance == 0) user_distance = 10;  // default if parse fails
        is_recording = 0;
        prev_sample = 0;
        object_detected = 0;
        prev_detected = 0;

    } else if (cmd[0] == 'S') {
        mode = 0;
        is_recording = 0;
        object_detected = 0;
        prev_detected = 0;
    }
}

/*
 * TIM2 period elapsed ISR — fires every ~60ms.
 * Sends the ultrasonic trigger pulse here (non-blocking),
 * then sets a flag for the main loop to measure the echo.
 * This separates trigger timing (ISR) from echo measurement (main loop).
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2 && mode == 'D') {
        HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_SET);
        delay_us(10);
        HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);
        ultrasonic_trigger_flag = 1;
    }
}

/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM16_Init();
    MX_TIM2_Init();
    MX_USART2_UART_Init();
    MX_SPI1_Init();

    /* USER CODE BEGIN 2 */
    HAL_TIM_Base_Start(&htim16);
    HAL_TIM_Base_Start_IT(&htim2);  // TIM2 runs with interrupt for ultrasonic trigger

    /* Blink LED to signal ready */
    HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
    HAL_Delay(500);
    HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN WHILE */

        /* --- 1. Non-blocking command receive from Python --- */
        uint8_t byte;
        if (HAL_UART_Receive(&huart2, &byte, 1, 0) == HAL_OK) {
            if (byte == '\n') {
                /* End of command — null terminate and process */
                rx_buffer[rx_index] = '\0';
                process_command(rx_buffer);
                rx_index = 0;
            } else {
                if (rx_index < RX_BUFFER_SIZE - 1) {
                    rx_buffer[rx_index++] = (char)byte;
                }
            }
        }

        /* --- 2. Echo measurement (triggered by TIM2 ISR flag) --- */
        if (ultrasonic_trigger_flag && mode == 'D') {
            ultrasonic_trigger_flag = 0;

            /* Wait for ECHO to go HIGH (with timeout) */
            uint32_t timeout = 100000;
            while (HAL_GPIO_ReadPin(ECHO_GPIO_Port, ECHO_Pin) == 0 && timeout--);

            if (timeout > 0) {
                /* Measure echo pulse width */
                __HAL_TIM_SET_COUNTER(&htim16, 0);
                timeout = 100000;
                while (HAL_GPIO_ReadPin(ECHO_GPIO_Port, ECHO_Pin) == 1 && timeout--);

                uint16_t duration = __HAL_TIM_GET_COUNTER(&htim16);
                float distance = (duration * 0.034f) / 2.0f;

                /* Schmitt trigger with ±2cm margin around user_distance */
                uint16_t margin = 2;
                uint16_t start_threshold = (user_distance > margin) ? user_distance - margin : 0;
                uint16_t stop_threshold  = user_distance + margin;

                prev_detected = object_detected;

                if (!object_detected && distance < start_threshold) {
                    object_detected = 1;
                    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);
                } else if (object_detected && distance > stop_threshold) {
                    object_detected = 0;
                    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
                }

                /* Object just left range — send 0xFF stop byte to Python */
                if (prev_detected == 1 && object_detected == 0) {
                    uint8_t stop_byte = 0xFF;
                    HAL_UART_Transmit(&huart2, &stop_byte, 1, HAL_MAX_DELAY);
                }

            } else {
                /* Timeout — nothing in range */
                prev_detected = object_detected;
                object_detected = 0;

                if (prev_detected == 1) {
                    uint8_t stop_byte = 0xFF;
                    HAL_UART_Transmit(&huart2, &stop_byte, 1, HAL_MAX_DELAY);
                    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
                }
            }
        }

        /* --- 3. Audio transfer --- */

        /*
         * Distance mode: stream audio via SPI while object is detected.
         * SPI with timeout=0 is non-blocking — returns immediately if no data,
         * so it never stalls the main loop the way UART polling did.
         */
        if (mode == 'D' && object_detected) {
            uint8_t data;
            if (HAL_SPI_Receive(&hspi1, &data, 1, 0) == HAL_OK) {
                uint8_t filtered = (data + prev_sample) / 2;
                if (filtered > 254) filtered = 254;     // Reserve 0xFF as stop byte
                HAL_UART_Transmit(&huart2, &filtered, 1, 0);
                prev_sample = data;
            }
        }

        /*
         * Manual mode: stream audio via SPI continuously.
         */
        else if (mode == 'M') {
            uint8_t data;
            if (HAL_SPI_Receive(&hspi1, &data, 1, 0) == HAL_OK) {
                uint8_t filtered = (data + prev_sample) / 2;
                if (filtered > 254) filtered = 254;     // Reserve 0xFF as stop byte
                HAL_UART_Transmit(&huart2, &filtered, 1, 0);
                prev_sample = data;
            }
        }

        /* USER CODE END WHILE */
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        Error_Handler();

    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 16;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
        Error_Handler();

    HAL_RCCEx_EnableMSIPLLMode();
}

static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_SLAVE;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES_RXONLY;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
        Error_Handler();
}

static void MX_TIM2_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    /* TIM2 fires every ~60ms to trigger ultrasonic ping */
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 31999;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 59;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
        Error_Handler();

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
        Error_Handler();

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
        Error_Handler();
}

static void MX_TIM16_Init(void)
{
    /* TIM16 used as microsecond counter for echo pulse measurement */
    htim16.Instance = TIM16;
    htim16.Init.Prescaler = 31;
    htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim16.Init.Period = 65535;
    htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim16.Init.RepetitionCounter = 0;
    htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
        Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
    /* 921600 baud to match working group — required for Task 3/4 sample rates */
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 921600;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart2) != HAL_OK)
        Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

    /* ECHO pin — input */
    GPIO_InitStruct.Pin = ECHO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ECHO_GPIO_Port, &GPIO_InitStruct);

    /* TRIG pin — output */
    GPIO_InitStruct.Pin = TRIG_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TRIG_GPIO_Port, &GPIO_InitStruct);

    /* LED */
    GPIO_InitStruct.Pin = LD3_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif