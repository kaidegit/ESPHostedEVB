/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "memorymap.h"
#include "octospi.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "elog.h"
#include "sfud.h"
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

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static const char *const TAG = "bootloader";
volatile uint32_t global_stack_top;
volatile uint32_t global_vector_addr;
volatile uint32_t global_entry_addr;

__STATIC_FORCEINLINE void JumpToApp(uint32_t stack_top, uint32_t vector_addr, uint32_t entry_addr) {
    // copy them to avoid use the var in stack after stack changing
    global_stack_top = stack_top;
    global_vector_addr = vector_addr;
    global_entry_addr = entry_addr;

    elog_i(TAG, "stack_top: 0x%08x", global_stack_top);
    elog_i(TAG, "vector_addr: 0x%08x", global_vector_addr);
    elog_i(TAG, "entry_addr: 0x%08x", global_entry_addr);

    HAL_MPU_Disable();
    SCB_DisableDCache();
    SCB_DisableICache();

    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (uint8_t i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    __enable_irq();

    __set_MSP(global_stack_top);
    __set_CONTROL(0);

    SCB->VTOR = global_vector_addr;

    ((void (*)(void)) (global_entry_addr))();

}

__STATIC_FORCEINLINE void EntryApp() {
    extern sfud_err qspi_entry_memory_mapped_mode(sfud_flash *flash);
    sfud_flash *flash = sfud_get_device(SFUD_MAIN_FLASH);
    qspi_entry_memory_mapped_mode(flash);

//    /* Set OTFDEC Mode */
//    if (HAL_OTFDEC_RegionSetMode(&hotfdec1, OTFDEC_REGION1, OTFDEC_REG_MODE_INSTRUCTION_OR_DATA_ACCESSES) != HAL_OK) {
//        Error_Handler();
//    }
//
//    /* Set OTFDEC Key */
//    if (HAL_OTFDEC_RegionSetKey(&hotfdec1, OTFDEC_REGION1, key) != HAL_OK) {
//        Error_Handler();
//    }
//
//    OTFDEC_RegionConfigTypeDef Config;
//    /* Configure and lock region,enable OTFDEC decryption */
//    Config.Nonce[0] = iv[0];
//    Config.Nonce[1] = iv[1];
//    Config.StartAddress = OCTOSPI1_BASE;
//    Config.EndAddress = OCTOSPI1_BASE + 1 * 1024 * 1024;
//    Config.Version = 0xABBA;
//    if (HAL_OTFDEC_RegionConfig(&hotfdec1, OTFDEC_REGION1, &Config, OTFDEC_REG_CONFIGR_LOCK_ENABLE) != HAL_OK) {
//        Error_Handler();
//    }

    uint32_t *stack_top = (uint32_t *) (OCTOSPI1_BASE);
    uint32_t *entry_addr = (uint32_t *) (OCTOSPI1_BASE + sizeof(uint32_t));
    uint32_t vector_addr = OCTOSPI1_BASE;
    JumpToApp(*stack_top, vector_addr, *entry_addr);
}
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

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

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
  MX_OCTOSPI1_Init();
  MX_USART2_UART_Init();
  MX_SPI2_Init();
  /* USER CODE BEGIN 2 */
    // note: qspi freq is set a little too low
    /* initialize EasyLogger */
    elog_init();
    /* set EasyLogger log format */
    elog_set_fmt(ELOG_LVL_ASSERT, ELOG_FMT_ALL);
    elog_set_fmt(ELOG_LVL_ERROR, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_WARN, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_DEBUG, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    /* start EasyLogger */
    elog_start();

    if (sfud_init() != SFUD_SUCCESS) {
        elog_e(TAG, "SFUD init failed!");
    }
    sfud_qspi_fast_read_enable(sfud_get_device(SFUD_MAIN_FLASH), 4);

//    char buf[100];
//    sfud_read(sfud_get_device(SFUD_MAIN_FLASH), 0, 100, buf);
//    elog_i(TAG, "Read 100 bytes from flash:");
//    elog_hexdump(TAG, 16, buf, 100);

    EntryApp();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1) {
        elog_i(TAG, "Hello World!");
        HAL_Delay(1000);
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 110;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 5;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
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
    while (1) {
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
