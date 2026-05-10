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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <math.h>

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
TIM_HandleTypeDef htim16;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static uint8_t mode = 0;                // 0 = Waiting, M = Manual, D = Distance
static uint8_t is_recording = 0;        // Only required for distance
static uint8_t distance_threshold = 10; // Detection threshold in cm (default 10)

static uint8_t buf[3] = {128, 128, 0};  // Moving Average Filter (Taken from Task 1)
static uint8_t mean = 128;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM16_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_TIM16_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
 HAL_TIM_Base_Start(&htim16);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
 

  while (1)
  {

    // Wait for python to send recording mode
    if (mode == 0){
        uint8_t cmd = 0;

        // Every 100ms tries to receive mode command
        if (HAL_UART_Receive(&huart2, &cmd, 1, 100) == HAL_OK){
            // Once command has been found
            if (cmd == 'M' || cmd == 'D'){
                // Reset to defaults at start of new recording
                mode = cmd;
                is_recording = 0;

                // For distance mode, read the threshold byte sent by Python
                if (cmd == 'D') {
                    uint8_t thresh = 10;
                    if (HAL_UART_Receive(&huart2, &thresh, 1, 200) == HAL_OK) {
                        distance_threshold = thresh;
                    } else {
                        distance_threshold = 10; // fallback to default
                    }
                }

                buf[0] = 128;
                buf[1] = 128;

                // Flushes any old bits from sampling STM
                uint8_t flush;
                while (HAL_UART_Receive(&huart1, &flush, 1,  5) == HAL_OK);
            }
        }
        continue;   // Restart loop - either re-check for mode, or begin using newly set mode
    }

    // Check for Stop byte from Python
    {
        uint8_t cmd = 0;

        if (HAL_UART_Receive(&huart2, &cmd, 1, 0) == HAL_OK){
          // Check for all valid commands in case command is sent during this block of code  
          if (cmd == 'S' || cmd == 'M' || cmd == 'D'){
                // Change mode to waiting if 'S' otherwise set to the specified mode
                mode = (cmd == 'S') ? 0: cmd;
                is_recording = 0;
              
                // Flush sampling STM
                uint8_t flush;
                while (HAL_UART_Receive(&huart1, &flush, 1,  5) == HAL_OK);
                buf[0] = 128; buf[1] = 128;                                 // Reset moving average filter
                continue;

            }
        }   
    }

    /*  
        Manual Mode
        Streams until python sends stop byte
        Basically is Task 1 processing code
    */
    if (mode == 'M'){
        if (HAL_UART_Receive(&huart1, &buf[2], 1, 5) == HAL_OK){

            buf[0] = buf[1];
            buf[1] = buf[2];
            mean  = (buf[0] + buf[1]) / 2;

            HAL_UART_Transmit(&huart2, &mean, 1, HAL_MAX_DELAY);
        }
    }

    /*
        Distance Mode
        Streams whilst within 10cm 
        Taken from Ultrasonic polling   
    */

    else if (mode == 'D'){

    /* Trigger pulse - 10µs */
    HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, 1);
    __HAL_TIM_SET_COUNTER(&htim16, 0); /* pass address of timer handle*/
    while (__HAL_TIM_GET_COUNTER(&htim16) < 10); /* cant use hal delay, as its in ms*/
    HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, 0);
    /* setting high for 10us 'triggers' the pulse*/

    /* Wait for ECHO to go HIGH */
    __HAL_TIM_SET_COUNTER(&htim16, 0); /*reset the timer again*/
    while (HAL_GPIO_ReadPin(ECHO_GPIO_Port, ECHO_Pin) == 0) { /*Loops within timeout until echo goes high = precise timer reset*/
        if (__HAL_TIM_GET_COUNTER(&htim16) > 30000) break; 
    }

    /* Measure echo pulse width */
    __HAL_TIM_SET_COUNTER(&htim16, 0); /*reset timer again*/
    while (HAL_GPIO_ReadPin(ECHO_GPIO_Port, ECHO_Pin) == 1) { /*Echo stays high until it receives signal, or timeout*/
        if (__HAL_TIM_GET_COUNTER(&htim16) > 38000) break; /*timeout for maximum distance of the specific sensor*/
    }
    uint32_t echo_time = __HAL_TIM_GET_COUNTER(&htim16); /*now we can get the time taken*/

    /* Distance formula: distance(cm) = echo time(µs) / 58 */
    uint32_t distance_cm = echo_time / 58;
    
  /* Schmitt trigger - state check */
  if (!is_recording && distance_cm <= distance_threshold) 
  {
    // OBJECT DETECTED: Flip the switch to ON
    is_recording = 1;

    // Flush bits to ensure clean recording
    uint8_t flush;
    while (HAL_UART_Receive(&huart1, &flush, 1,  5) == HAL_OK);

    // Reset moving average filter
    buf[0] = 128;
    buf[1] = 128;
  } 
  else if (is_recording && distance_cm > (distance_threshold + 5)) 
  {
      // OBJECT REMOVED: Flip the switch to OFF only after it clears 15cm
      is_recording = 0;

      // Send stop byte to python
      uint8_t stop_byte = 0xFF;
      HAL_UART_Transmit(&huart2, &stop_byte, 1, HAL_MAX_DELAY); // if we were recording, and move to far, send stop byte
  }

  /* Transfer audio if within range */
  if (is_recording) 
  {
      // Changed from ultrasonic to include moving average filter from Task 1
      // Pull from Sampling STM (UART1)
      if (HAL_UART_Receive(&huart1, &buf[2], 1, 5) == HAL_OK) 
      {
        buf[0] = buf[1];
        buf[1] = buf[2];
        mean = (buf[0] + buf[1]) / 2;

          // Forward to Python (USART2)
          HAL_UART_Transmit(&huart2, &mean, 1, HAL_MAX_DELAY);
      }
      // No Delay here: Maintain high sample rate!
  } 
  else
  {
    /* Wait 60ms between readings */
    HAL_Delay(60); //once stop byte sent, we go back to sampling every 60ms
  }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_10;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
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
  htim16.Instance = TIM16;
  htim16.Init.Prescaler = 31;
  htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim16.Init.Period = 65535;
  htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
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
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
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
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

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
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : TRIG_Pin */
  GPIO_InitStruct.Pin = TRIG_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(TRIG_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ECHO_Pin */
  GPIO_InitStruct.Pin = ECHO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ECHO_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
#ifdef USE_FULL_ASSERT
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