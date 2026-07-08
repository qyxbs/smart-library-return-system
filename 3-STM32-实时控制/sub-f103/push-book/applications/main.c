/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-30     RT-Thread    first version
 */

#include <rtthread.h>
#include "uart.h"
#include <board.h>
#include <rtdevice.h>
#include <rtdbg.h>
#include "limite.h"
#include "adc.h"
#define DBG_TAG "main"
#define DBG_LVL DBG_LOG

//09 1前0后
//08 1推书方向
int main(void)
{


/*
   while(1)
    {
        build_speed_cmd(0x08,0,200,5);
                                     rt_thread_mdelay(2000);
                               //      rt_device_write(uart2_dev,0,spot_08, 7);
                                                                   rt_thread_mdelay(2000);

    }

   */
 rt_thread_t tid_limite = rt_thread_create("limit_hdl",limit_switch_thread,RT_NULL,512,1,10);
   rt_thread_startup(tid_limite);
  // rt_thread_t tid_limite1 = rt_thread_create("limit_hdl1",limit_switch_thread1,RT_NULL,512,1,10);
   //   rt_thread_startup(tid_limite1);
   //   rt_thread_t tid_limite2 = rt_thread_create("limit_hdl21",limit_switch_thread2,RT_NULL,512,1,10);
    //        rt_thread_startup(tid_limite2);
   rt_thread_t tid_uart = rt_thread_create("limit_hdl7",uart_IQ,RT_NULL,512,2,10);
      rt_thread_startup(tid_uart);
// rt_thread_t tid_ad = rt_thread_create("limit_hd2",push_book,RT_NULL,512,4,10);
 //  rt_thread_startup(tid_ad);
   rt_thread_t tid_push = rt_thread_create("limit_hd3",push,RT_NULL,512,4,10);
   rt_thread_startup(tid_push);
   rt_thread_t tid_back = rt_thread_create("limit_hd4",back,RT_NULL,512,3,10);
   rt_thread_startup(tid_back);
   rt_thread_t tid_trail = rt_thread_create("limit_hd5",trail,RT_NULL,1024,2,10);
   rt_thread_startup(tid_trail);
   rt_thread_t tid_rest = rt_thread_create("limit_hd25",rest,RT_NULL,512,1,5);
      rt_thread_startup(tid_rest);

   // rt_thread_mdelay(10000);
 //   while(1)
  //  {
  //      rt_thread_mdelay(200);
   //                               rt_device_write(uart2_dev,0,start, 4);
  //                                rt_thread_mdelay(200);
  //  }

    /*
    rt_thread_mdelay(3000);
    while(1)
    {
        build_speed_cmd_9(1,100,5);
           rt_thread_mdelay(500);
           rt_device_write(uart1_dev, 0,spot_09, 7);
           rt_thread_mdelay(2000);
           build_speed_cmd_9(0,100,5);
                       rt_thread_mdelay(500);
                       rt_device_write(uart1_dev, 0,spot_09, 7);
                       rt_thread_mdelay(2000);

    }
*/

   //trail();

    return RT_EOK;
}
