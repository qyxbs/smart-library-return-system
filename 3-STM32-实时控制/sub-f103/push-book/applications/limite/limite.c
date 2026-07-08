/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-30     17590       the first version
 */
#include <limite.h>
#include <uart.h>
#include "adc.h"

#define limit_switch_pin   GET_PIN(A,7)
#define limit_switch_pin1   GET_PIN(A,5)
#define limit_switch_pin2   GET_PIN(A,6)
uint8_t i,one=0,o=0;
uint8_t scan[4]={0xCB,0XCA,0XCA,0XCB};



rt_sem_t limit_sem;
rt_sem_t limit_sem1;
rt_sem_t limit_sem2;

uint8_t send[2]={0xFA,0XFA};
uint8_t receive[2]={0XFB,0XFB};

void limit_switch_IRQ(void *args)
{

       // rt_pin_write(led,PIN_HIGH);
        rt_sem_release(limit_sem);


}

void limit_switch_IRQ1(void *args)
{

       // rt_pin_write(led,PIN_HIGH);

        rt_sem_release(limit_sem1);


}

void limit_switch_IRQ2(void *args)
{

       // rt_pin_write(led,PIN_HIGH);

        rt_sem_release(limit_sem2);

}

int limit_switch_Iint(void)
{

    limit_sem = rt_sem_create("limit", 0, RT_IPC_FLAG_FIFO);
    limit_sem1 = rt_sem_create("limit1", 0, RT_IPC_FLAG_FIFO);
    limit_sem2 = rt_sem_create("limit2", 0, RT_IPC_FLAG_FIFO);

 //   rt_pin_write(limit_switch_pin, PIN_HIGH);

    rt_pin_mode(limit_switch_pin, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(limit_switch_pin, PIN_IRQ_MODE_FALLING,limit_switch_IRQ , RT_NULL);
    rt_pin_irq_enable(limit_switch_pin, PIN_IRQ_ENABLE);

    rt_pin_mode(limit_switch_pin1, PIN_MODE_INPUT_PULLUP);
        rt_pin_attach_irq(limit_switch_pin1, PIN_IRQ_MODE_FALLING,limit_switch_IRQ1 , RT_NULL);
        rt_pin_irq_enable(limit_switch_pin1, PIN_IRQ_ENABLE);

        rt_pin_mode(limit_switch_pin2, PIN_MODE_INPUT_PULLUP);
            rt_pin_attach_irq(limit_switch_pin2, PIN_IRQ_MODE_RISING_FALLING,limit_switch_IRQ2 , RT_NULL);
            rt_pin_irq_enable(limit_switch_pin2, PIN_IRQ_ENABLE);
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
        if(rt_pin_read(limit_switch_pin) == PIN_LOW)
        {
            // 真正发一次数据
            if(one==0)
            {

             rt_device_write(uart2_dev,0,spot_08, 7);
            rt_thread_mdelay(200);
            rt_device_write(uart2_dev,0,spot_08, 7);
            rt_thread_mdelay(200);
            build_speed_cmd(0x08,0,200,50);
            rt_thread_mdelay(400);
            build_speed_cmd(0x08,0,200,50);
            rt_thread_mdelay(400);

            rt_device_write(uart2_dev,0,spot_08, 7);
           rt_thread_mdelay(200);
           rt_device_write(uart2_dev,0,spot_08, 7);
           rt_thread_mdelay(2000);
           rt_device_write(uart2_dev,0,scan, 4);
          rt_thread_mdelay(200);
           // build_speed_cmd(0x08,0,200,5);
           //rt_thread_mdelay(200);

          //  rt_device_write(uart2_dev,0,start, 4);
         //   rt_thread_mdelay(200);
          //  rt_sem_release(push_sem);
            j=0;
            one++;
            }

        }

        // 等待开关松开，防止一直触发
        while(rt_pin_read(limit_switch_pin) == PIN_LOW)
        {
            rt_thread_mdelay(10);
        }
    }
}

void limit_switch_thread1(void *parameter)
{
    while(1)
    {
        // 等待中断信号
        rt_sem_take(limit_sem1, RT_WAITING_FOREVER);

        // 消抖 + 确认是真按下
        rt_thread_mdelay(20);
        if(rt_pin_read(limit_switch_pin1) == PIN_LOW)
        {
            // 真正发一次数据


                     rt_device_write(uart2_dev,0,spot_08, 7);
                        rt_thread_mdelay(200);
                        rt_device_write(uart2_dev,0,spot_08, 7);
                        rt_thread_mdelay(200);



            }

        }

        // 等待开关松开，防止一直触发
        while(rt_pin_read(limit_switch_pin1) == PIN_LOW)
        {
            rt_thread_mdelay(10);
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
        if(rt_pin_read(limit_switch_pin2) == PIN_LOW)
        {
            // 真正发一次数据


             if(o==0)
             {
                 o=1;
                 rt_device_write(uart2_dev,0,send, 2);
                           rt_thread_mdelay(200);

             }else  {

                o=0;
                rt_device_write(uart2_dev,0,receive, 2);
                          rt_thread_mdelay(200);
            }



            }

        }

        // 等待开关松开，防止一直触发
        while(rt_pin_read(limit_switch_pin2) == PIN_LOW)
        {
            rt_thread_mdelay(10);
        }
    }


INIT_BOARD_EXPORT(limit_switch_Iint);
