/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-31     17590       the first version
 */
#include <limite.h>
#include <uart.h>
#include "work.h"

#define limit_switch_pin   GET_PIN(A,4)
#define limit_switch_pin1   GET_PIN(A,6)
#define limit_switch_pin3   GET_PIN(A,7)
#define limit_switch_pin2   GET_PIN(E,5)


uint8_t spot_1[7]={0xFA, 0x66, 0xF6, 0x00, 0x00, 0x00, 0x56};
rt_base_t irq_trig_pin;

rt_sem_t limit_sem;
rt_sem_t limit_sem2;

void limit_switch_IRQ(void *args)
{


        rt_sem_release(limit_sem);


}

void limit_switch_IRQ2(void *args)
{


        rt_sem_release(limit_sem2);


}


int limit_switch_Iint(void)
{

    limit_sem = rt_sem_create("limit", 0, RT_IPC_FLAG_FIFO);
    limit_sem2 = rt_sem_create("limit4", 0, RT_IPC_FLAG_FIFO);

 //   rt_pin_write(limit_switch_pin, PIN_HIGH);

    rt_pin_mode(limit_switch_pin, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(limit_switch_pin, PIN_IRQ_MODE_FALLING,limit_switch_IRQ , RT_NULL);
    rt_pin_irq_enable(limit_switch_pin, PIN_IRQ_ENABLE);
    rt_pin_mode(limit_switch_pin1, PIN_MODE_INPUT_PULLUP);
        rt_pin_attach_irq(limit_switch_pin1, PIN_IRQ_MODE_FALLING,limit_switch_IRQ2 , RT_NULL);
       rt_pin_irq_enable(limit_switch_pin1, PIN_IRQ_ENABLE);
       rt_pin_mode(limit_switch_pin2, PIN_MODE_INPUT_PULLUP);
           rt_pin_attach_irq(limit_switch_pin2, PIN_IRQ_MODE_FALLING,limit_switch_IRQ2 , RT_NULL);
           rt_pin_irq_enable(limit_switch_pin2, PIN_IRQ_ENABLE);
           rt_pin_mode(limit_switch_pin3, PIN_MODE_INPUT_PULLUP);
             rt_pin_attach_irq(limit_switch_pin3, PIN_IRQ_MODE_FALLING,limit_switch_IRQ , RT_NULL);
             rt_pin_irq_enable(limit_switch_pin3, PIN_IRQ_ENABLE);
  //  rt_pin_mode(led, PIN_MODE_OUTPUT);

    return 0;
}

void limit_switch_thread(void *parameter)
{
    while(1)
    {

        // 等待中断信号
        rt_sem_take(limit_sem, RT_WAITING_FOREVER);

        // 消抖 + 确认是真按下
        rt_thread_mdelay(20);
       // rt_kprintf("ok");
        if(rt_pin_read(limit_switch_pin) == PIN_LOW|| rt_pin_read(limit_switch_pin3) == PIN_LOW)
        {
//rt_kprintf("ok");
           // rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
             //   rt_device_write(uart3_dev,0,spot_1,7);
            //    rt_mutex_release(uart3_mutex);
                 //          rt_thread_delay(200);
                           rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
                           rt_device_write(uart3_dev,0,spot_1,7);
                           rt_mutex_release(uart3_mutex);
                             rt_thread_delay(200);
                             rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
                                                        rt_device_write(uart3_dev,0,spot_1,7);
                                                        rt_mutex_release(uart3_mutex);
                                                          rt_thread_delay(200);
                            // rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
                         //    rt_device_write(uart3_dev,0,spot_1,7);
                         //    rt_mutex_release(uart3_mutex);
                         //    rt_thread_delay(200);
                             build_rel_pulse_cmd(1,200,2500);
                             build_rel_pulse_cmd(1,200,2500);



        }
        /*else if (rt_pin_read(limit_switch_pin1) == PIN_LOW || rt_pin_read(limit_switch_pin2) == PIN_LOW)
        {
      //      rt_kprintf("ok");
            rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
            rt_device_write(uart3_dev,0,spot_six,7);
            rt_mutex_release(uart3_mutex);
            rt_thread_delay(200);

           // rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
            build_pulse_rel_cmd(0x06,1,200,50,3000);
         //   rt_mutex_release(uart3_mutex);
           // rt_device_write(uart3_dev,0,spot_six,7);
             //           rt_thread_delay(200);
                       rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
                        rt_device_write(uart3_dev,0,spot_six,7);

                        rt_mutex_release(uart3_mutex);
                        rt_thread_delay(200);

        }
*/


        // 等待开关松开，防止一直触发
        while(rt_pin_read(limit_switch_pin) == PIN_LOW || rt_pin_read(limit_switch_pin3) == PIN_LOW )
        {

            rt_thread_mdelay(10);
        }
    }
}

void limit_switch_thread2(void *parameter)
{
    while(1)
    {

        // 等待中断信号
        rt_sem_take(limit_sem2, RT_WAITING_FOREVER);

        // 消抖 + 确认是真按下
        rt_thread_mdelay(20);
     if ( rt_pin_read(limit_switch_pin2) == PIN_LOW)
        {
      //      rt_kprintf("ok");
            rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
            rt_device_write(uart3_dev,0,spot_six,7);
            rt_mutex_release(uart3_mutex);
            rt_thread_delay(200);
            rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
                        rt_device_write(uart3_dev,0,spot_six,7);
                        rt_mutex_release(uart3_mutex);
                        rt_thread_delay(200);

           // rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
            build_pulse_rel_cmd(0x06,1,200,50,2650);

            build_pulse_rel_cmd(0x06,1,200,50,2650);
         //   rt_mutex_release(uart3_mutex);
           // rt_device_write(uart3_dev,0,spot_six,7);
             //           rt_thread_delay(200);


        }else if(rt_pin_read(limit_switch_pin1) == PIN_LOW )
        {

                       rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
                       rt_device_write(uart3_dev,0,spot_six,7);
                       rt_mutex_release(uart3_mutex);
                       rt_thread_delay(200);
                       rt_device_write(uart3_dev,0,spot_six,7);
                       rt_mutex_release(uart3_mutex);
                       rt_thread_delay(200);


                       build_pulse_rel_cmd(0x06,0,200,50,28000);
                       build_pulse_rel_cmd(0x06,0,200,50,28000);


        }


     while(rt_pin_read(limit_switch_pin2) == PIN_LOW || rt_pin_read(limit_switch_pin1) == PIN_LOW )
             {

                 rt_thread_mdelay(10);
             }

    }
}

INIT_BOARD_EXPORT(limit_switch_Iint);
