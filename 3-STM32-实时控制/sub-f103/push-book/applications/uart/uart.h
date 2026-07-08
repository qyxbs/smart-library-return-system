/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-30     17590       the first version
 */
#ifndef APPLICATIONS_UART_UART_H_
#define APPLICATIONS_UART_UART_H_

#include <board.h>
#include <rtdevice.h>
#include <rtdbg.h>
#include <rtthread.h>


#define LOC(t) 0x01<<t
extern rt_device_t uart1_dev;
extern rt_device_t uart2_dev;
rt_err_t uart_input(rt_device_t dev, rt_size_t size);
extern rt_sem_t uart_sem;
extern uint8_t out[7];
void build_speed_cmd(uint8_t addr, uint8_t dir, uint16_t speed, uint8_t acc);
void uart_IQ(void *parameted);

uint8_t spot_1[7];
extern rt_sem_t rest_sem;

void build_speed_cmd_9(uint8_t dir_9, uint16_t speed_9, uint8_t acc_9);

extern rt_sem_t push_sem;
extern rt_sem_t back_sem;
extern rt_sem_t push_book_sem;
extern rt_sem_t trail_sem;
extern rt_event_t strat_event;
#endif /* APPLICATIONS_UART_UART_H_ */
