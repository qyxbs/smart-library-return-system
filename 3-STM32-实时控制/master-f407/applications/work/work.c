/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-24     17590       the first version
 */
#include "work.h"
uint8_t close_ok[4]={0xCC,0X33,0X33,0XFB};
uint8_t start_seize[4]={0xAA,0x11,0x11,0XFF};
//uint8_t rest_ok[4]={0xB3,0xA1,0xB2,0XC1};
uint8_t i=0;
void seize_open(void *parameter)
{
    while(1)
    {
        rt_sem_take(start_sem, RT_WAITING_FOREVER);
        rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
         //rt_device_write(uart3_dev,0,start_seize,4);
       // rt_thread_delay(200);
        build_speed_cmd(0x66,1,500,2);
                rt_thread_delay(200);
                build_speed_cmd(0x66,1,500,2);
                                rt_thread_delay(200);
                build_speed_cmd(0x66,1,500,2);
                    rt_thread_delay(200);
            rt_mutex_release(uart3_mutex);

    }





}

void open_close_enter(void *parameter)
{
    while(1)
    {
        rt_sem_take(open_close, RT_WAITING_FOREVER);
        rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
      //  build_rel_pulse_cmd(1,200,50000);
      //  rt_thread_delay(200);
       // build_rel_pulse_cmd(1,200,50000);
      // rt_thread_delay(10000);

        build_speed_cmd(0x66,1,500,2);
        rt_thread_delay(200);
        build_speed_cmd(0x66,1,500,2);
                rt_thread_delay(200);
        build_speed_cmd(0x66,1,500,2);
            rt_thread_delay(200);
      //  rt_device_write(uart3_dev,0,close_ok,4);
       // rt_thread_delay(200);
            rt_mutex_release(uart3_mutex);





       // rt_kprintf("okokok");


    }


}

void rest(void *parameter)
{
    rt_thread_delay(3000);
    rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
    build_speed_cmd(0x04,0,500,2);//4号，1上，0下
    rt_thread_delay(200);
    build_speed_cmd(0x66,0,200,2);
    rt_mutex_release(uart3_mutex);
    build_speed_cmd(0x66,0,200,2);
    rt_mutex_release(uart3_mutex);
    rt_thread_delay(1000);
    rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
    build_speed_cmd(0x06,0,200,2);//6号，1左，0右。背对机器
    rt_thread_delay(200);
    build_speed_cmd(0x06,0,200,2);//6号，1左，0右。背对机器
        rt_thread_delay(200);
     rt_device_write(uart3_dev,0,start_seize,4);
     rt_thread_delay(200);
    rt_mutex_release(uart3_mutex);


         //   rt_thread_delay(5000);


}
/*
void send_close_ok(void *parameter)
{
    while(1)
    {
        rt_sem_take(send_sem, RT_WAITING_FOREVER);
        rt_thread_delay(200);
        rt_device_write(uart3_dev,0,close_ok,4);
        rt_thread_delay(200);

    }



}
*/
void seize_back(void *parameter)
{

    while(1)
    {
        rt_sem_take(back_sem, RT_WAITING_FOREVER);
       // build_speed_cmd(0x66,0,100,2);
        //        rt_thread_delay(200);
        //        build_speed_cmd(0x66,0,100,2);
       //         rt_thread_delay(200);
    //    rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
         // build_rel_pulse_cmd(0,300,60000);
       // rt_thread_delay(200);
      //  build_rel_pulse_cmd(0,300,60000);
       // rt_thread_delay(4000);
        build_speed_cmd(0x66,0,500,2);
        rt_thread_delay(200);
        build_speed_cmd(0x66,0,500,2);
        rt_thread_delay(200);
      //  rt_mutex_release(uart3_mutex);
        rt_thread_delay(4000);
      //  rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
        build_speed_cmd(0x04,1,675,50);//4号，1上，0下。
        rt_thread_delay(200);
        build_speed_cmd(0x04,1,675,50);//4号，1上，0下。
        rt_thread_delay(200);
     //   rt_mutex_release(uart3_mutex);
        m=0;
       // rt_thread_delay(10000);
       // scan_tid();




         //   rt_mutex_release(uart3_mutex);
           // rt_mutex_take(uart2_mutex, RT_WAITING_FOREVER);

            //    rt_mutex_release(uart2_mutex);

      //  build_speed_cmd(0x06,1,500,2);//6号，1左，0右。背对机器
      //  rt_thread_delay(1500);
      //  rt_device_write(uart3_dev,0,spot_six,7);
      //  rt_thread_delay(200);
      //  build_speed_cmd(0x06,0,500,2);//6号，1左，0右。背对机器
     //   rt_thread_delay(1500);
     //   rt_device_write(uart3_dev,0,spot_six,7);
     //   rt_thread_delay(200);
    }



}


void next_tid(void *parameter)
{

   while(1)
   {
       rt_sem_take(next_sem, RT_WAITING_FOREVER);

     //  rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
       build_speed_cmd(0x66,0,300,2);
                        rt_thread_delay(200);
       build_speed_cmd(0x66,0,500,2);
                 rt_thread_delay(200);
          build_speed_cmd(0x66,0,300,2);
          rt_thread_delay(4000);
          build_speed_cmd(0x04,0,500,2);//4号，1上，0下
          rt_thread_delay(200);
          build_speed_cmd(0x04,0,500,2);//4号，1上，0下
           rt_thread_delay(200);
          build_speed_cmd(0x04,0,500,2);//4号，1上，0下
                    rt_thread_delay(200);
          build_speed_cmd(0x06,0,350,2);//6号，1左，0右。背对机器
          rt_thread_delay(200);
          build_speed_cmd(0x06,0,350,2);//6号，1左，0右。背对机器
           rt_thread_delay(200);
           build_speed_cmd(0x06,0,350,2);//6号，1左，0右。背对机器
                      rt_thread_delay(200);
           rt_device_write(uart3_dev,0,start_seize,4);
           rt_thread_delay(200);
           x=0;
           y=0;
       //   rt_mutex_release(uart3_mutex);



   }




}

void Get_book(void *parameter)
{

    while(1)
    {
        rt_sem_take(get_sem, RT_WAITING_FOREVER);
        rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
        build_speed_cmd(0x04,1,650,2);//4号，1上，0下
                  rt_thread_delay(200);
           build_speed_cmd(0x04,1,650,2);//4号，1上，0下
          rt_thread_delay(200);
          build_speed_cmd(0x04,1,650,2);//4号，1上，0下
          rt_thread_delay(200);
          rt_mutex_release(uart3_mutex);

    }



}

void Get_next_book(void *parameter)
{

    while(1)
    {
        rt_sem_take(get_next_sem, RT_WAITING_FOREVER);
       // rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);

        build_speed_cmd(0x66,0,200,2);
                               rt_thread_delay(200);
              build_speed_cmd(0x66,0,200,2);
                        rt_thread_delay(200);
                 build_speed_cmd(0x66,0,200,2);
                 rt_thread_delay(4000);
        build_speed_cmd(0x04,0,650,2);//4号，1上，0下
                  rt_thread_delay(200);
           build_speed_cmd(0x04,0,650,2);//4号，1上，0下
          rt_thread_delay(200);
          build_speed_cmd(0x04,0,650,2);//4号，1上，0下
          rt_thread_delay(200);
          build_speed_cmd(0x06,1,200,2);//6号，1左，0右。背对机器
                    rt_thread_delay(200);
                    build_speed_cmd(0x06,1,200,2);//6号，1左，0右。背对机器
                     rt_thread_delay(200);
                     build_speed_cmd(0x06,1,200,2);//6号，1左，0右。背对机器
                                rt_thread_delay(33000);
         // rt_mutex_release(uart3_mutex);
          rt_sem_release(get_end_sem);
    }



}


void Get_end(void *parameter)
{

    while(1)
    {
        rt_sem_take(get_end_sem, RT_WAITING_FOREVER);

                build_pulse_rel_cmd(0x04,1,500,50,210000);
                      build_pulse_rel_cmd(0x04,1,650,50,210000);
                      build_pulse_rel_cmd(0x04,1,650,50,210000);
                      rt_thread_delay(10000);
        build_rel_pulse_cmd(1,500,80000);
         build_rel_pulse_cmd(1,500,80000);
          build_rel_pulse_cmd(1,500,80000);
          rt_thread_delay(10000);
          rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
                         rt_device_write(uart3_dev,0,end,4);
                         rt_thread_delay(200);
                         rt_mutex_release(uart3_mutex);
                         rt_thread_delay(5000);
                                       z=0;
                                       action=0;
                                       state=0;
                                       rt_sem_release(next_sem);


    }



}


