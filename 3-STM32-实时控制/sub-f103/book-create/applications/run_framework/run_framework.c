/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-02     17590       the first version
 */
#include  <run_framework.h>
#include  <adc.h>
#include  <uart.h>
#include  <limit.h>

uint8_t open_ok[4]={0XBB,0X22,0X22,0XFA};
uint8_t shut_ok[4]={0XDD,0X44,0X44,0XFC};
uint8_t spot_1[7]={0xFA, 0x66, 0xF6, 0x00, 0x00, 0x00, 0x56};


uint32_t ad_val;
uint8_t i=0,j=0,z=0;
void start_open(void *parameted)
{

    while(1)
    {
        rt_sem_take(seize_open_sem, RT_WAITING_FOREVER);


        while(1)
       {

        if(i==0)
       {
            rt_thread_mdelay(100);
            rt_device_write(uart1_dev, 0,run, 7);
            rt_thread_mdelay(100);
            i++;

        }


ad_val=rt_adc_read(ad_dev, 0);
               rt_thread_mdelay(100);

               if(ad_val<=1700)
                     {
                       if(i==1)
                       {

                           rt_thread_mdelay(100);
                          rt_device_write(uart1_dev, 0,spot, 7);
                          rt_thread_mdelay(100);
                          rt_device_write(uart1_dev, 0,reset, 7);
                          rt_thread_mdelay(200);
                          rt_device_write(uart1_dev, 0,reset, 7);
                                                    rt_thread_mdelay(200);
                          rt_device_write(uart1_dev, 0,reset, 7);//张开
                          rt_thread_mdelay(8600);
                          rt_device_write(uart1_dev, 0,spot, 7);
                          rt_thread_mdelay(200);

                      //    rt_device_write(uart2_dev, 0,open_ok, 4);
                       //   rt_thread_mdelay(200);
                          i=0;
                         // z=0;
                          break;

                       }

                     }



    }

}

}





//}
void shut(void *parameted)
{
    while(1)
    {

        rt_sem_take(shut_sem, RT_WAITING_FOREVER);
    //    rt_device_write(uart2_dev, 0,shut_ok, 4);
    //    rt_thread_mdelay(200);

        while(1)
        {
            if(j==0)
                   {
                      rt_device_write(uart2_dev,0,spot_1,7);
                      rt_thread_delay(200);
                      rt_device_write(uart2_dev,0,spot_1,7);
                       rt_thread_delay(200);
                       rt_device_write(uart2_dev,0,spot_1,7);
                        rt_thread_delay(200);
                      // rt_thread_mdelay(200);
                       rt_device_write(uart1_dev, 0,run, 7);
                       rt_thread_mdelay(200);
                       j++;

                   }
            ad_val=rt_adc_read(ad_dev, 0);
            rt_thread_mdelay(100);

                    if(ad_val<=2000)
                    {
                        rt_thread_mdelay(200);
                        rt_device_write(uart1_dev, 0,spot, 7);
                        rt_thread_mdelay(200);

                        if(state==1)
                        {
                         rt_device_write(uart2_dev, 0,shut_ok, 4);
                         rt_thread_mdelay(200);

                        }
                            else if (state==2)
                                {
                                     rt_device_write(uart2_dev,0,get_book,4);
                                     rt_thread_delay(200);
                               }
                        j=0;

                        break;
                    }




                        //加速这里释放另外一个信号量

                    }




        }

    }






/*

void startrun_thread(void *parameted)
{
  //  while(1)
  //  {
      //  rt_sem_take(limit_sem, RT_WAITING_FOREVER);这里接收信号量或者是事件再执行
    while(1)
    {
        ad_val=rt_adc_read(ad_dev, 0);
    //    rt_kprintf("ad_val=%d\n",ad_val);
        rt_thread_mdelay(100);
        if(ad_val<=1700)
        {
            rt_thread_mdelay(100);
            rt_device_write(uart2_dev, 0,spot, 7);
            //rt_device_write(uart1_dev, 0,reset, 7);
            rt_thread_mdelay(100);

            //加速这里释放另外一个信号量
 //           break;
        }
       rt_thread_mdelay(10);
    }
 //   }

}


*/

void end_open(void *parameted)
{
    while(1)
    {
        rt_sem_take(seize_end, RT_WAITING_FOREVER);
       // if(z==0)
      //  {

            j=1;
            rt_device_write(uart1_dev, 0,reset, 7);//张开
                              rt_thread_mdelay(6000);
                              rt_device_write(uart1_dev, 0,spot, 7);
                              rt_thread_mdelay(6000);
                              one=0;
                              j=0;
                      //        z++;

      //  }

                             // state=0;



    }


}




