/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-02     17590       the first version
 */
#ifndef APPLICATIONS_RUN_FRAMEWORK_RUN_FRAMEWORK_H_
#define APPLICATIONS_RUN_FRAMEWORK_RUN_FRAMEWORK_H_

#include <board.h>
#include <rtdevice.h>
#include <rtdbg.h>
#include <rtthread.h>

void startrun_thread(void *parameted);
extern uint8_t open_ok[4];
extern uint8_t shut_ok[4];
void start_open(void *parameted);
void shut(void *parameted);
extern uint32_t ad_val;
extern uint8_t i;
void start_open2(void *parameted);
void end_open(void *parameted);


#endif /* APPLICATIONS_RUN_FRAMEWORK_RUN_FRAMEWORK_H_ */
