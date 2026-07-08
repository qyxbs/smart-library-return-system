/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-16     RT-Thread    first version
 */

#include <rtthread.h>
#include "uart.h"
#include <board.h>
#include <rtdevice.h>
#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>
#include "work.h"
#include "limite.h"
//uint8_t b[10]={0x08,0xc6,0x04,0x08,0x00,0xf2,0xc9,0x00,0xfd,0x6b};
//uint8_t c[4]={0xDD,0X44,0X44,0XFC};
int main(void)
{





    rt_thread_t tid_limite = rt_thread_create("limit_hdl01",limit_switch_thread,RT_NULL,512,1,10);
    rt_thread_startup(tid_limite);
    rt_thread_t tid_limite2 = rt_thread_create("limit_hdl016",limit_switch_thread2,RT_NULL,512,1,10);
        rt_thread_startup(tid_limite2);
    rt_thread_t tid_abscissa = rt_thread_create("limit_hd41",abscissa,RT_NULL,512,3,15);
     rt_thread_startup(tid_abscissa);
     rt_thread_t tid_ordinate = rt_thread_create("limit_hdl1011",ordinate,RT_NULL,512,3,6);
     rt_thread_startup(tid_ordinate);
    rt_thread_t uart_IQ1 = rt_thread_create("limit_hdl",uart_IQ,RT_NULL,512,2,10);
    rt_thread_startup(uart_IQ1);
   // rt_thread_t uart_IQ2 = rt_thread_create("limit_hd11",uart2_IQ,RT_NULL,512,2,9);
   // rt_thread_startup(uart_IQ2);

 rt_thread_t start_capture = rt_thread_create("limit_hd2",seize_open,RT_NULL,512,4,10);
 rt_thread_startup(start_capture);
   // rt_thread_t tid_close = rt_thread_create("limit_hd3",open_close_enter,RT_NULL,512,4,10);
   // rt_thread_startup(tid_close);
    rt_thread_t tid_back = rt_thread_create("limit_hd4",seize_back,RT_NULL,512,4,10);
    rt_thread_startup(tid_back);
    rt_thread_t tid_rest = rt_thread_create("limit_hdl1",rest,RT_NULL,512,3,10);
    rt_thread_startup(tid_rest);
    rt_thread_t next_tid1 = rt_thread_create("limit_hdl19",next_tid,RT_NULL,512,3,10);
        rt_thread_startup(next_tid1);
        rt_thread_t get_tid1 = rt_thread_create("limit_hdl29",Get_book,RT_NULL,512,4,10);
                rt_thread_startup(get_tid1);
                rt_thread_t get_next_tid1 = rt_thread_create("limit_hdl29",Get_next_book,RT_NULL,512,4,10);
              rt_thread_startup(get_next_tid1);

              rt_thread_t get_end_tid1 = rt_thread_create("limit_hdl25",Get_end,RT_NULL,512,4,10);
           rt_thread_startup(get_end_tid1);





   // while(1)
 //   {

  //      rt_device_write(uart2_dev,0,scan2,9);
    //                                       rt_thread_delay(2000);
 //  / }





   return 0;
}
