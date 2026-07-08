/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-30     17590       the first version
 */
#ifndef APPLICATIONS_LIMITE_LIMITE_H_
#define APPLICATIONS_LIMITE_LIMITE_H_

#include <board.h>
#include <rtdevice.h>
#include <rtdbg.h>
#include <rtthread.h>


extern rt_sem_t limit_sem;
extern uint8_t one;
void limit_switch_thread2(void *parameter);
void limit_switch_thread1(void *parameter);
void limit_switch_thread(void *parameter);


#endif /* APPLICATIONS_LIMITE_LIMITE_H_ */
