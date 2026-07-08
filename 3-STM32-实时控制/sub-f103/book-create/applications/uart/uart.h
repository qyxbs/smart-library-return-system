/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-01     17590       the first version
 */
#ifndef APPLICATIONS_UART_UART_H_
#define APPLICATIONS_UART_UART_H_


#include <board.h>
#include <rtdevice.h>
#include <rtdbg.h>
#include <rtthread.h>
#include "run_framework.h"



extern rt_device_t uart1_dev;
extern rt_device_t uart2_dev;

rt_err_t uart_input(rt_device_t dev, rt_size_t size);
extern rt_sem_t uart_sem;
extern rt_sem_t seize_open_sem;
extern rt_sem_t seize_open_sem1;
extern rt_sem_t shut_sem;
extern rt_mutex_t uart3_mutex;
extern uint8_t state;
extern rt_sem_t seize_end;
void build_speed_cmd(uint8_t addr, uint8_t dir, uint16_t speed, uint8_t acc);
void build_pulse_rel_cmd(uint8_t addr, uint8_t dir, uint16_t speed, uint8_t acc, uint32_t rel_pulses);

void uart_IQ(void *parameted);
#endif /* APPLICATIONS_UART_UART_H_ */
