/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-02     17590       the first version
 */
#include <adc.h>

rt_adc_device_t ad_dev;
int adc_Init(void)
{

   ad_dev=(rt_adc_device_t) rt_device_find("adc1");
   rt_adc_enable(ad_dev, 0);

   return 0;

}






INIT_BOARD_EXPORT(adc_Init);
