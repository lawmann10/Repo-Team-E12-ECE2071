/* Task 3 - Processing STM
 *
 * Receives 10-bit audio via SPI slave from Sampling STM at 44.1kHz
 * Pipeline: outlier rejection -> 4-tap moving average -> 2:1 downsample -> 10-bit to 8-bit
 * Outputs 8-bit audio at 22.05kHz via USART2 (460800 baud) to PC
 *
 * Peripheral summary:
 *   SPI1   - Slave, 16-bit frames, CPOL=Low, CPHA=1Edge (matches Sampling STM master)
 *   USART2 - 460800 baud, communicates with Python
 *   TIM16  - 1MHz tick (Prescaler=31 on 32MHz clock) for ultrasonic timing
 *   GPIO   - TRIG_Pin (PA1 output), ECHO_Pin (PA0 input), LD3_Pin (PB3 output)
 *
 * Clock: MSI 4MHz -> PLL (x16 / 2) -> 32MHz SYSCLK
 */

#include "main.h"
#include <stdint.h>

/* ── Peripheral handles ─────────────────────────────────────────────────── */
TIM_HandleTypeDef  htim16;
SPI_HandleTypeDef  hspi1;
UART_HandleTypeDef huart2;

/* ── Pin definitions (match your CubeMX pin assignments) ───────────────── */
#define TRIG_Pin        GPIO_PIN_1
#define TRIG_GPIO_Port  GPIOA
#define ECHO_Pin        GPIO_PIN_0
#define ECHO_GPIO_Port  GPIOA
#define LD3_Pin         GPIO_PIN_3
#define LD3_GPIO_Port   GPIOB

/* ── Processing parameters (tune these to optimise audio quality) ───────── */
#define MA_LEN              4       // moving average filter length
#define OUTLIER_THRESHOLD   150     // max deviation from running mean (0-1023 scale)

/* ── State variables ────────────────────────────────────────────────────── */
static uint8_t  mode               = 0;    // 0=idle, 'M'=manual, 'D'=distance
static uint8_t  is_recording       = 0;
static uint8_t  distance_threshold = 10;   // cm, sent by Python at mode start

static uint16_t ma_buf[MA_LEN];
static uint32_t ma_sum;
static uint8_t  ma_idx;
static uint8_t  decimate_count;
static uint16_t running_mean;

/* ── Forward declarations ───────────────────────────────────────────────── */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM16_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);

/* ── Filter helpers ─────────────────────────────────────────────────────── */
static void reset_filters(void)
{
    running_mean  = 512;            // 10-bit midpoint
    ma_sum        = 512 * MA_LEN;
    ma_idx        = 0;
    decimate_count = 0;
    for (int i = 0; i < MA_LEN; i++) ma_buf[i] = 512;
}

/* Process one 10-bit sample through the pipeline.
 * Returns 0-255 output byte, or -1 if this sample is consumed by downsampling. */
static int16_t process_sample(uint16_t raw)
{
    raw &= 0x03FF;  // ensure 10-bit range

    /* Outlier rejection: clamp to running mean if too far away */
    uint32_t diff = (raw > running_mean) ? (raw - running_mean)
                                         : (running_mean - raw);
    if (diff > OUTLIER_THRESHOLD)
        raw = running_mean;

    /* IIR running mean update (alpha = 1/16) */
    running_mean = (uint16_t)((running_mean * 15 + raw) / 16);

    /* 4-tap moving average */
    ma_sum        -= ma_buf[ma_idx];
    ma_buf[ma_idx]  = raw;
    ma_sum        += raw;
    ma_idx         = (ma_idx + 1) % MA_LEN;
    uint16_t filtered = (uint16_t)(ma_sum / MA_LEN);

    /* 2:1 downsample — output every second sample */
    decimate_count++;
    if (decimate_count >= 2) {
        decimate_count = 0;
        return (int16_t)(filtered >> 2);    // 10-bit -> 8-bit (divide by 4)
    }
    return -1;
}

/* Send one byte to PC with escape encoding (distance mode only).
 *   0xFF 0xFF = stop signal — handled by caller, not here
 *   0xFF 0x00 = escaped audio byte of value 255 */
static void send_escaped(uint8_t byte)
{
    if (byte == 0xFF) {
        uint8_t escaped[] = {0xFF, 0x00};
        HAL_UART_Transmit(&huart2, escaped, 2, HAL_MAX_DELAY);
    } else {
        HAL_UART_Transmit(&huart2, &byte, 1, HAL_MAX_DELAY);
    }
}

/* ── Main ───────────────────────────────────────────────────────────────── */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM16_Init();
    MX_SPI1_Init();
    MX_USART2_UART_Init();

    HAL_TIM_Base_Start(&htim16);

    while (1)
    {
        /* ── Idle: wait for mode command from Python ── */
        if (mode == 0) {
            uint8_t cmd = 0;
            if (HAL_UART_Receive(&huart2, &cmd, 1, 100) == HAL_OK) {
                if (cmd == 'M' || cmd == 'D') {
                    mode         = cmd;
                    is_recording = 0;
                    reset_filters();

                    if (cmd == 'D') {
                        uint8_t thresh = 10;
                        if (HAL_UART_Receive(&huart2, &thresh, 1, 200) == HAL_OK)
                            distance_threshold = thresh;
                        else
                            distance_threshold = 10;
                    }

                    /* Drain any stale SPI data from Sampling STM */
                    uint16_t flush;
                    while (HAL_SPI_Receive(&hspi1, (uint8_t*)&flush, 1, 1) == HAL_OK);
                }
            }
            continue;
        }

        /* ── Check for stop command from Python ('S') ── */
        {
            uint8_t cmd = 0;
            if (HAL_UART_Receive(&huart2, &cmd, 1, 0) == HAL_OK) {
                if (cmd == 'S') {
                    mode         = 0;
                    is_recording = 0;
                    uint16_t flush;
                    while (HAL_SPI_Receive(&hspi1, (uint8_t*)&flush, 1, 1) == HAL_OK);
                    continue;
                }
            }
        }

        /* ── Manual mode: stream until Python sends 'S' ── */
        if (mode == 'M') {
            uint16_t raw;
            if (HAL_SPI_Receive(&hspi1, (uint8_t*)&raw, 1, 5) == HAL_OK) {
                int16_t out = process_sample(raw);
                if (out >= 0) {
                    uint8_t byte = (uint8_t)out;
                    HAL_UART_Transmit(&huart2, &byte, 1, HAL_MAX_DELAY);
                }
            }
        }

        /* ── Distance mode: proximity-triggered recording ── */
        else if (mode == 'D') {

            /* Trigger HC-SR04 pulse (10µs) */
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
            uint32_t distance_cm = __HAL_TIM_GET_COUNTER(&htim16) / 58;

            /* Schmitt trigger: start at threshold, stop at threshold+5 */
            if (!is_recording && distance_cm <= distance_threshold) {
                is_recording = 1;
                reset_filters();
                uint16_t flush;
                while (HAL_SPI_Receive(&hspi1, (uint8_t*)&flush, 1, 1) == HAL_OK);

            } else if (is_recording && distance_cm > (uint32_t)(distance_threshold + 5)) {
                is_recording = 0;
                uint8_t stop_seq[] = {0xFF, 0xFF};
                HAL_UART_Transmit(&huart2, stop_seq, 2, HAL_MAX_DELAY);
            }

            if (is_recording) {
                uint16_t raw;
                if (HAL_SPI_Receive(&hspi1, (uint8_t*)&raw, 1, 5) == HAL_OK) {
                    int16_t out = process_sample(raw);
                    if (out >= 0)
                        send_escaped((uint8_t)out);
                }
            } else {
                HAL_Delay(60);
            }
        }
    }
}

/* ── Clock: MSI 4MHz -> PLL x16 / 2 -> 32MHz SYSCLK ───────────────────── */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.LSEState            = RCC_LSE_ON;
    RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange       = RCC_MSIRANGE_6;    // 4MHz
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM            = 1;
    RCC_OscInitStruct.PLL.PLLN            = 16;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;     // 4*16/2 = 32MHz
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1);

    HAL_RCCEx_EnableMSIPLLMode();
}

/* ── TIM16: 1MHz tick for ultrasonic (32MHz / 32 = 1MHz) ───────────────── */
static void MX_TIM16_Init(void)
{
    htim16.Instance                = TIM16;
    htim16.Init.Prescaler          = 31;
    htim16.Init.CounterMode        = TIM_COUNTERMODE_UP;
    htim16.Init.Period             = 65535;
    htim16.Init.ClockDivision      = TIM_CLOCKDIVISION_DIV1;
    htim16.Init.RepetitionCounter  = 0;
    htim16.Init.AutoReloadPreload  = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim16);
}

/* ── SPI1: Slave, 16-bit, matches Sampling STM master config ───────────── */
static void MX_SPI1_Init(void)
{
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_SLAVE;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
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
    HAL_SPI_Init(&hspi1);
}

/* ── USART2: 460800 baud to PC ──────────────────────────────────────────── */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance            = USART2;
    huart2.Init.BaudRate       = 460800;
    huart2.Init.WordLength     = UART_WORDLENGTH_8B;
    huart2.Init.StopBits       = UART_STOPBITS_1;
    huart2.Init.Parity         = UART_PARITY_NONE;
    huart2.Init.Mode           = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling   = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    HAL_UART_Init(&huart2);
}

/* ── GPIO: TRIG (output), ECHO (input), LD3 (output) ───────────────────── */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD3_GPIO_Port,  LD3_Pin,  GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = TRIG_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TRIG_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin  = ECHO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ECHO_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = LD3_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}