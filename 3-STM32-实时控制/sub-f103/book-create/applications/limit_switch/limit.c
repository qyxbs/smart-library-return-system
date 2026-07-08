/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-01     17590       the first version
 */
#include <limit.h>
#include <uart.h>



#define limit_switch_pin   GET_PIN(A,6)
#define limit_switch_pin1   GET_PIN(A,4)
//#define led                GET_PIN(C,13)
rt_sem_t limit_sem;
uint8_t rest_ok[4]={0xB3,0xA1,0xB2,0XC1};
uint8_t get_book[4]={0xDA,0xDA,0xDA,0XDA};
uint8_t spot[7] ={0xFA,0x05,0xF6,0x00,0x00,0x00,0xF5};
uint8_t reset[7]={0xFA,0x05,0xF6,0x00,0x40,0x05,0x3A};
uint8_t run[7]={0xFA,0x05,0xF6,0x80,0x80,0x05,0xFA};
uint8_t send_next[4]={0xEE,0XEE,0XEE,0XEE};
uint8_t spot_fuor[7] ={0xFA,0x04,0xF6,0x00,0x00,0x00,0xF4};
uint8_t i,l=0;
uint8_t one=0;

void motot_reset(void *parametet)
{

//while(1)
//{
    if(i==0)
    {

        build_speed_cmd(0x04,0,2000,2);
             rt_thread_mdelay(200);
             i++;

    }

         //    rt_kprintf("ok");

//}


}

void limit_switch_IRQ(void *args)
{

       // rt_pin_write(led,PIN_HIGH);
        rt_sem_release(limit_sem);


}

int limit_switch_Iint(void)
{

    limit_sem = rt_sem_create("limit", 0, RT_IPC_FLAG_FIFO);

    rt_pin_mode(limit_switch_pin, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(limit_switch_pin, PIN_IRQ_MODE_FALLING,limit_switch_IRQ , RT_NULL);
    rt_pin_irq_enable(limit_switch_pin, PIN_IRQ_ENABLE);

    rt_pin_mode(limit_switch_pin1, PIN_MODE_INPUT_PULLUP);
        rt_pin_attach_irq(limit_switch_pin1, PIN_IRQ_MODE_FALLING,limit_switch_IRQ , RT_NULL);
        rt_pin_irq_enable(limit_switch_pin1, PIN_IRQ_ENABLE);
  //  rt_pin_mode(led, PIN_MODE_OUTPUT);

    return 0;
}

void limit_switch_thread(void *parameter)
{
    while(1)
    {

            rt_sem_take(limit_sem, RT_WAITING_FOREVER);
            rt_thread_mdelay(15);

                   if(rt_pin_read(limit_switch_pin) == PIN_LOW)
                   {

                        //   rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);

                           rt_device_write(uart2_dev,0,spot_fuor,7);
                           rt_thread_delay(200);
                           rt_device_write(uart2_dev,0,spot_fuor,7);
                           rt_thread_delay(200);
                           build_speed_cmd(0x04,1,1000,50);
                           rt_thread_delay(200);
                           build_speed_cmd(0x04,1,1000,50);
                           rt_thread_delay(4000);
                           rt_device_write(uart2_dev,0,spot_fuor,7);
                           rt_thread_delay(200);
                           rt_device_write(uart2_dev,0,spot_fuor,7);
                           rt_thread_delay(200);
                           if (l==0) {

                               rt_device_write(uart2_dev,0,rest_ok,4);
                               rt_thread_delay(200);
                               l++;
                             //  rt_device_write(uart2_dev,0,rest_ok,4);
                            //   rt_thread_delay(200);
                            //   rt_device_write(uart2_dev,0,rest_ok,4);
                            //   rt_thread_delay(200);

                        }
                           if(state==1)
                           {

                             rt_device_write(uart2_dev,0,rest_ok,4);
                             rt_thread_delay(200);
                            // rt_device_write(uart2_dev,0,send_next,4);
                          //   rt_thread_delay(200);

                           }//else if(state==2)
                         //  {

                         //    rt_device_write(uart2_dev,0,get_book,4);
                         //    rt_thread_delay(200);

                         //  }


                   //    build_pulse_rel_cmd(0x04,1,500,50,250000);
                       //    rt_mutex_release(uart3_mutex);

                   }

                   if(rt_pin_read(limit_switch_pin1) == PIN_LOW)
                      {
                       if(one==0)
                                  {




                               rt_sem_release(shut_sem);
                               one++;


                                  }

                       }

    }

}

INIT_BOARD_EXPORT(limit_switch_Iint);
