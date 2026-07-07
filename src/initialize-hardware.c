/*
 * This file is part of the µOS++ distribution.
 * (https://github.com/micro-os-plus)
 */

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_cortex.h"
#include "diag/trace.h"

// Forward declarations
void __initialize_hardware(void);
void SystemClock_Config(void);

/**
 * @brief Application hardware initialization routine.
 * Called early from _start(), right after data & bss init.
 */
void __initialize_hardware(void)
{
  // Initialise the HAL Library
  HAL_Init();

  // Enable HSE Oscillator and activate PLL with HSE as source
  SystemClock_Config();

  // Update SystemCoreClock global RAM variable
  SystemCoreClockUpdate();
}


/**
 * @brief System Clock Configuration safe for STM32F413
 */
void __attribute__((weak)) SystemClock_Config(void)
{
  // Enable Power Control clock
  __PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitTypeDef RCC_OscInitStruct = {0};

  // Safe Fallback: Let's use the internal HSI (16MHz) oscillator to bypass
  // potential external crystal stability issues while testing.
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;

  // Set up safe, conservative frequencies (84 MHz)
  RCC_OscInitStruct.PLL.PLLM = 16;  // 16MHz / 16 = 1MHz
  RCC_OscInitStruct.PLL.PLLN = 336; // 1MHz * 336 = 336MHz
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4; // 336MHz / 4 = 84MHz
  RCC_OscInitStruct.PLL.PLLQ = 7;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;

  // Clock dividers for 84MHz Core
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;  // HCLK = 84MHz
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;   // PCLK1 = 42MHz (Max allowed)
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;   // PCLK2 = 84MHz

  // Latency 2 is ideal and stable for 84MHz operations on an F413
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);

  // Reconfigure SysTick
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
}

#if defined(USE_FULL_ASSERT)
void assert_failed (uint8_t* file, uint32_t line)
{
  trace_printf("Wrong parameters value: file %s on line %d\r\n", file, line);
  while (1);
}
#endif
