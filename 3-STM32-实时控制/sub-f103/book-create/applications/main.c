/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-02-28     RT-Thread    first version
 */

#include <rtthread.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>
#include <board.h>
#include <rtdevice.h>
#include <uart.h>
#include <limit.h>
#include "adc.h"
#include <run_framework.h>

int main(void)
{


       rt_thread_t tid = rt_thread_create("limit_hdl1",limit_switch_thread,RT_NULL,512,1,5);
        rt_thread_startup(tid);
        rt_thread_t tid_IQ = rt_thread_create("limit_hd4",uart_IQ,RT_NULL,512,2,10);
        rt_thread_startup(tid_IQ);
        rt_thread_t tid_seize_open = rt_thread_create("limit_hd2",start_open,RT_NULL,512,3,9);
        rt_thread_startup(tid_seize_open);
        rt_thread_t tid_shut = rt_thread_create("limit_hd3",shut,RT_NULL,512,4,10);
        rt_thread_startup(tid_shut);
        rt_thread_t tid_end = rt_thread_create("limit_hd8",end_open,RT_NULL,512,5,10);
        rt_thread_startup(tid_end);









}



