/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

TIM_HandleTypeDef htim16;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static uint8_t mode = 0;
static uint8_t is_recording = 0;
static uint8_t buf[3] = {128, 128, 0};
static uint8_t mean = 128;

// Interrupt-driven receive variables
volatile uint8_t rx_cmd = 0;
volatile uint8_t cmd_received = 0;
/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM16_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);

/* USER CODE BEGIN 0 */
// This fires automatically whenever a byte arrives on huart2 (from Python)
// Shoudlnt miss byte now
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        if (rx_cmd == 'M' || rx_cmd == 'D' || rx_cmd == 'S') {
            cmd_received = 1;   // Flag for main loop to handle
        }
        // Re-arm the interrupt to listen for the next byte
        HAL_UART_Receive_IT(&huart2, &rx_cmd, 1);
    }
}
/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM16_Init();
    MX_USART2_UART_Init();
    MX_USART1_UART_Init();

    /* USER CODE BEGIN 2 */
    HAL_TIM_Base_Start(&htim16);
    // Start listening for commands from Python via interrupt
    HAL_UART_Receive_IT(&huart2, &rx_cmd, 1);
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN WHILE */

        // Check if interrupt has flagged a new command from Python
        if (cmd_received) {
            cmd_received = 0;           // Clear the flag
            uint8_t cmd = rx_cmd;       // Copy to local variable immediately
                                        // in case interrupt fires again

            mode = (cmd == 'S') ? 0 : cmd;
            is_recording = 0;
            buf[0] = 128;
            buf[1] = 128;

            // Flush stale samples from Sampling STM
            uint8_t flush;
            while (HAL_UART_Receive(&huart1, &flush, 1, 5) == HAL_OK);
            continue;
        }

        // Still waiting for initial mode command
        if (mode == 0) {
            HAL_Delay(10);  // Small delay to avoid busy-waiting while idle
            continue;
        }

        /*
            Manual Mode
            Streams until Python sends stop byte
        */
        if (mode == 'M') {
            if (HAL_UART_Receive(&huart1, &buf[2], 1, 5) == HAL_OK) {
                buf[0] = buf[1];
                buf[1] = buf[2];
                mean = (buf[0] + buf[1]) / 2;
                if (mean > 254) mean = 254;     // Reserve 0xFF as stop byte

                HAL_UART_Transmit(&huart2, &mean, 1, HAL_MAX_DELAY);
            }
        }

        /*
            Distance Mode
            Streams whilst within range
        */
        else if (mode == 'D') {

            /* Trigger pulse - 10µs */
            HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, 1);
            __HAL_TIM_SET_COUNTER(&htim16, 0);
            while (__HAL_TIM_GET_COUNTER(&htim16) < 10);
            HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, 0);

            /* Wait for ECHO to go HIGH */
            __HAL_TIM_SET_COUNTER(&htim16, 0);
            while (HAL_GPIO_ReadPin(ECHO_GPIO_Port, ECHO_Pin) == 0) {
                if (__HAL_TIM_GET_COUNTER(&htim16) > 30000) break;
            }

            /* Measure echo pulse width */
            __HAL_TIM_SET_COUNTER(&htim16, 0);
            while (HAL_GPIO_ReadPin(ECHO_GPIO_Port, ECHO_Pin) == 1) {
                if (__HAL_TIM_GET_COUNTER(&htim16) > 38000) break;
            }
            uint32_t echo_time = __HAL_TIM_GET_COUNTER(&htim16);
            uint32_t distance_cm = echo_time / 58;

            /* Schmitt trigger - state check */
            if (!is_recording && distance_cm <= 10) {
                is_recording = 1;

                uint8_t flush;
                while (HAL_UART_Receive(&huart1, &flush, 1, 5) == HAL_OK);

                buf[0] = 128;
                buf[1] = 128;
            }
            else if (is_recording && distance_cm > 15) {
                is_recording = 0;

                uint8_t stop_byte = 0xFF;
                HAL_UART_Transmit(&huart2, &stop_byte, 1, HAL_MAX_DELAY);
            }

            /* Transfer audio if within range */
            if (is_recording) {
                if (HAL_UART_Receive(&huart1, &buf[2], 1, 5) == HAL_OK) {
                    buf[0] = buf[1];
                    buf[1] = buf[2];
                    mean = (buf[0] + buf[1]) / 2;
                    if (mean > 254) mean = 254;     // Reserve 0xFF as stop byte

                    HAL_UART_Transmit(&huart2, &mean, 1, HAL_MAX_DELAY);
                }
            }
            else {
                HAL_Delay(60);
            }
        }
        /* USER CODE END WHILE */
    }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        Error_Handler();

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_10;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
        Error_Handler();
}

static void MX_TIM16_Init(void)
{
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
    huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart1) != HAL_OK)
        Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
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
    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = TRIG_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TRIG_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ECHO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ECHO_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif