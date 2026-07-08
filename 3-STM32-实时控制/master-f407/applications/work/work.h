/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-24     17590       the first version
 */
#ifndef APPLICATIONS_WORK_WORK_H_
#define APPLICATIONS_WORK_WORK_H_

#include <board.h>
#include <rtdevice.h>
#include <rtdbg.h>
#include <rtthread.h>
#include "errno.h"
#include "uart.h"
void Get_book(void *parameter);
void Get_next_book(void *parameter);
void Get_end(void *parameter);
extern uint8_t start_seize[4];
extern uint8_t rest_ok[4];
void seize_open(void *parameter);
void open_close_enter(void *parameter);
void seize_back(void *parameter);
extern uint8_t close_ok[4];
extern uint8_t start_seize[4];
void send_close_ok(void *parameter);
void rest(void *parameter);
void next_tid(void *parameter);
#endif /* APPLICATIONS_WORK_WORK_H_ */
