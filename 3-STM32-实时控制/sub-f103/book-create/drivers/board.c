/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-02-28     RealThread   first version
 */

#include <rtthread.h>
#include <board.h>
#include <drv_common.h>

#ifndef RT_WEAK
#define RT_WEAK rt_weak
#endif

RT_WEAK void rt_hw_board_init()
{
    extern void hw_board_init(char *clock_src, int32_t clock_src_freq, int32_t clock_target_freq);

    /* Heap initialization */
#if defined(RT_USING_HEAP)
    rt_system_heap_init((void *) HEAP_BEGIN, (void *) HEAP_END);
#endif

    hw_board_init(BSP_CLOCK_SOURCE, BSP_CLOCK_SOURCE_FREQ_MHZ, BSP_CLOCK_SYSTEM_FREQ_MHZ);

    /* Set the shell console output device */
#if defined(RT_USING_DEVICE) && defined(RT_USING_CONSOLE)
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif

    /* Board underlying hardware initialization */
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

}


/**
  * @brief ADC 底层硬件初始化（时钟、引脚）
  * @param hadc ADC 句柄指针
  */
void HAL_ADC_MspInit(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1)
    {
        /* 1. 使能 ADC1 时钟（ADC1 挂载在 APB2 总线上） */
        __HAL_RCC_ADC1_CLK_ENABLE();

        /* 2. 配置 PA0 为模拟模式（ADC1 通道 0） */
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = GPIO_PIN_0;          // PA0
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;    // 模拟模式
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

