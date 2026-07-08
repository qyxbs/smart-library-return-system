/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-01     17590       the first version
 */
#ifndef APPLICATIONS_LIMIT_SWITCH_LIMIT_H_
#define APPLICATIONS_LIMIT_SWITCH_LIMIT_H_

#include <board.h>
#include <rtdevice.h>
#include <rtdbg.h>
#include <rtthread.h>


extern rt_sem_t limit_sem;
extern  uint8_t spot[7];
extern  uint8_t reset[7];
extern   uint8_t run[7];
extern uint8_t  spot_fuor[7];
extern uint8_t one;
extern uint8_t get_book[4];
void limit_switch_thread(void *parameter);
void motot_reset(void *parametet);


#endif /* APPLICATIONS_LIMIT_SWITCH_LIMIT_H_ */
