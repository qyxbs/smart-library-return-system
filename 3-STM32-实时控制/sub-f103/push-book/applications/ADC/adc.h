/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-01     17590       the first version
 */
#ifndef APPLICATIONS_ADC_ADC_H_
#define APPLICATIONS_ADC_ADC_H_

#include <board.h>
#include <rtdevice.h>
#include <rtdbg.h>
#include <rtthread.h>
#include "uart.h"

extern rt_adc_device_t ad_dev;
extern void push_book(void *parameter);
extern uint8_t spot_08[7];
extern uint8_t spot_09[7];
extern uint8_t start[4];
extern uint8_t j;
void push(void *parameter);
void back(void *parameter);
void trail(void *parameter);
void rest(void *parameter);

#endif /* APPLICATIONS_ADC_ADC_H_ */
