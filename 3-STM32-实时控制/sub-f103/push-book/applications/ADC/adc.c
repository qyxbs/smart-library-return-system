/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-01     17590       the first version
 */
#include <adc.h>
#include "limite.h"
uint8_t j=0;
uint8_t spot_08[7]={0xFA,0x08,0xF6,0x00,0x00,0x00,0xF8};
uint8_t spot_09[7]={0xFA,0x09,0xF6,0x00,0x00,0x00,0xF9};
//uint8_t seize[4]={0xEE,0X55,0X55,0XEE};
uint8_t start[4]={0xFD,0X88,0X88,0XFD};
uint8_t ba=0;
uint32_t ad_val;
rt_adc_device_t ad_dev;
int adc_Init(void)
{

   ad_dev=(rt_adc_device_t) rt_device_find("adc1");
   rt_adc_enable(ad_dev, 0);

   return 0;

}

void push_book(void *parameter)
{



    while(1)
       {

           rt_sem_take(push_book_sem, RT_WAITING_FOREVER);


           while(1)
           {
               if(j==0)
                      {
                          build_speed_cmd(0x08,1,500,5);
                          j++;

                      }
               ad_val=rt_adc_read(ad_dev, 0);
               rt_thread_mdelay(100);

                       if(ad_val<=1700)
                       {
                           rt_thread_mdelay(200);
                           rt_device_write(uart2_dev,0,spot_08, 7);
                           rt_thread_mdelay(200);
                           j=0;
                           break;
                       }




           }


}

}

void trail(void *parameter)
{
    j=0;
    while(1)
              {
       rt_event_recv(strat_event, LOC(0)|LOC(1), RT_EVENT_FLAG_AND|RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, 0);
                  if(j==0)
                         {
                       //    rt_thread_mdelay(20000);
                         //    build_speed_cmd(0x08,1,500,5);
                             build_speed_cmd(0x08,1,300,50);
                             rt_thread_mdelay(200);
                             build_speed_cmd(0x08,1,300,50);
                             rt_thread_mdelay(200);
                             build_speed_cmd(0x08,1,300,50);
                             rt_thread_mdelay(200);
                             j++;
                             ba=0;

                         }
                //  ad_val=rt_adc_read(ad_dev, 0);
                //  rt_thread_mdelay(100);
                 // rt_kprintf("%u",ad_val);

                       //   if(ad_val<=4000)
                       //   {

                         //     rt_device_write(uart2_dev,0,spot_08, 7);
                          //    rt_thread_mdelay(200);
                         //     rt_device_write(uart2_dev,0,spot_08, 7);
                         //     rt_thread_mdelay(200);
                         //     rt_device_write(uart2_dev,0,start, 4);
                         //     rt_thread_mdelay(200);
                             // rt_device_write(uart2_dev,0,start, 4);
                             // rt_thread_mdelay(200);
                          //    rt_sem_release(push_sem);
                           //   j=0;
                            //  break;
                          }




              }



//}

void push(void *parameter)
{
    while(1)
    {
        rt_sem_take(push_sem, RT_WAITING_FOREVER);
        rt_thread_mdelay(200);
        build_speed_cmd_9(1,50,5);
        rt_thread_mdelay(750);
        rt_device_write(uart1_dev, 0,spot_09, 7);
        rt_thread_mdelay(200);
        //rt_device_write(uart2_dev,0,seize, 4);
       // rt_thread_mdelay(200);


    }



}

void back(void *parameter)
{
    while(1)
        {
            rt_sem_take(back_sem, RT_WAITING_FOREVER);
            if(ba==0)
            {
                rt_thread_mdelay(200);
            build_speed_cmd_9(0,50,5);
            rt_thread_mdelay(750);
            rt_device_write(uart1_dev, 0,spot_09, 7);
            rt_thread_mdelay(200);
            one=0;
            j=0;
            ba++;//下一次推书开始令BA=0；

            }

           // rt_sem_release(push_book_sem);


        }


}
void rest(void *parameter)
{
    rt_thread_mdelay(2000);
    build_speed_cmd(0x08,0,500,5);
    rt_thread_mdelay(200);
    build_speed_cmd(0x08,0,500,5);
    rt_thread_mdelay(200);
    build_speed_cmd(0x08,0,500,5);

   while(1)
   {
       rt_sem_take(rest_sem, RT_WAITING_FOREVER);
       rt_thread_mdelay(2000);
           build_speed_cmd(0x08,0,500,5);
           rt_thread_mdelay(200);
           build_speed_cmd(0x08,0,500,5);
           rt_thread_mdelay(200);
           build_speed_cmd(0x08,0,500,5);





   }




}




INIT_BOARD_EXPORT(adc_Init);
