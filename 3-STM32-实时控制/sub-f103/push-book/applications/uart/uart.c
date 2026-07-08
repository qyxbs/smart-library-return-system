/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-30     17590       the first version
 */
#include "uart.h"
#include "adc.h"
#define PACKET_SIZE 4

static uint8_t packet_buf[PACKET_SIZE];
static int packet_idx = 0;
uint8_t buf[4];
rt_event_t strat_event;
//推向夹子7号，推向旁边8号

rt_device_t uart1_dev;
rt_device_t uart2_dev;
uint8_t out[7];
uint8_t out_9[7];
uint8_t spot_1[7]={0xFA, 0x01, 0xF6, 0x00, 0x00, 0x00, 0xF1};

struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
struct serial_configure config2 = RT_SERIAL_CONFIG_DEFAULT;

rt_sem_t uart_sem;
rt_sem_t push_sem;
rt_sem_t back_sem;
rt_sem_t trail_sem;
rt_sem_t push_book_sem;
rt_sem_t rest_sem;

int uart_Iint(void)
{
          uart1_dev = rt_device_find("uart1");
          uart2_dev = rt_device_find("uart2");

          uart_sem = rt_sem_create("limit1", 0, RT_IPC_FLAG_FIFO);
          back_sem = rt_sem_create("limit2", 0, RT_IPC_FLAG_FIFO);
          push_sem = rt_sem_create("limit3", 0, RT_IPC_FLAG_FIFO);
          trail_sem = rt_sem_create("limit5", 0, RT_IPC_FLAG_FIFO);
          push_book_sem = rt_sem_create("limit4", 0, RT_IPC_FLAG_FIFO);
          rest_sem = rt_sem_create("limit6", 0, RT_IPC_FLAG_FIFO);

          strat_event=rt_event_create("start", RT_IPC_FLAG_PRIO);


          config.baud_rate = BAUD_RATE_38400;
          config.data_bits = DATA_BITS_8;
          config.stop_bits = STOP_BITS_1;
          config.parity    = PARITY_NONE;

          config2.baud_rate = BAUD_RATE_38400;
          config2.data_bits = DATA_BITS_8;
          config2.stop_bits = STOP_BITS_1;
          config2.parity    = PARITY_NONE;




          rt_device_open(uart1_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
          rt_device_open(uart2_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);

          rt_device_control(uart1_dev, RT_DEVICE_CTRL_CONFIG, &config);
          rt_device_control(uart2_dev, RT_DEVICE_CTRL_CONFIG, &config2);
          rt_device_set_rx_indicate(uart2_dev, uart_input);


          return 0;
}

rt_err_t uart_input(rt_device_t dev, rt_size_t size)
{

    rt_sem_release(uart_sem);

    return 0;
}

void build_speed_cmd(uint8_t addr, uint8_t dir, uint16_t speed, uint8_t acc) {
    // 限幅
    if (speed > 3000) speed = 3000;

    uint8_t speed_high = (speed >> 8) & 0x0F;   // 高4位
    uint8_t speed_low  = speed & 0xFF;          // 低8位
    uint8_t byte3 = (dir << 7) | speed_high;    // 方向+速度高4位

    out[0] = 0xFA;
    out[1] = addr;
    out[2] = 0xF6;
    out[3] = byte3;
    out[4] = speed_low;
    out[5] = acc;

    // 计算CRC (CHECKSUM8bit)
    uint8_t crc = 0;
    for (int i = 0; i < 6; i++) {
        crc += out[i];
    }
    out[6] = crc;

            rt_device_write(uart2_dev,0,out,7);
            rt_thread_delay(200);

}

void build_speed_cmd_9(uint8_t dir_9, uint16_t speed_9, uint8_t acc_9) {
    // 限幅
    if (speed_9 > 3000) speed_9 = 3000;

    uint8_t speed_high_9 = (speed_9 >> 8) & 0x0F;   // 高4位
    uint8_t speed_low_9  = speed_9 & 0xFF;          // 低8位
    uint8_t byte34 = (dir_9 << 7) | speed_high_9;    // 方向+速度高4位

    out_9[0] = 0xFA;
    out_9[1] = 0x09;
    out_9[2] = 0xF6;
    out_9[3] = byte34;
    out_9[4] = speed_low_9;
    out_9[5] = acc_9;

    // 计算CRC (CHECKSUM8bit)
    uint8_t crc_9 = 0;
    for (int i = 0; i < 6; i++) {
        crc_9 += out_9[i];
    }
    out_9[6] = crc_9;

            rt_device_write(uart1_dev,0,out_9,7);
            rt_thread_delay(200);

}

void uart_IQ(void *parameted)
{
    int len, i;
    while (1)
    {
        rt_sem_take(uart_sem, RT_WAITING_FOREVER);

        len = rt_device_read(uart2_dev, 0, buf, sizeof(buf));

        for (i = 0; i < len; i++)
        {
            // 关键：不是合法帧头直接丢弃，避免杂数据堆积乱包
            if (packet_idx == 0 && buf[i] != 0xDD && buf[i] != 0xB3 && buf[i] != 0xEE && buf[i] != 0xFD && buf[i] != 0xAA)//&& buf[i] != 0xCC
            {
                continue;
            }

            packet_buf[packet_idx++] = buf[i];

            // 收满一包
            if (packet_idx == PACKET_SIZE)
            {
                // 可选：打印一包数据方便调试
               // for(int z = 0; z < 4; z++)
            //    {
             //       rt_kprintf("0x%02x ", packet_buf[z]);
            //    }
            //    rt_kprintf("\r\n");

                // 开门
               // if(packet_buf[0]==0xCC && packet_buf[1]==0x33 && packet_buf[2]==0x33 && packet_buf[3]==0xFB)
               // {
               //     rt_sem_release(push_sem);
              //  }
                // 回位/报警
              //  else
                    if(packet_buf[0] == 0xDD && packet_buf[1] == 0x44 &&
                        packet_buf[2] == 0x44 && packet_buf[3] == 0xFC)
                {
                    rt_sem_release(back_sem);
                }else if (packet_buf[0] == 0xB3 && packet_buf[1] == 0xA1 &&
                        packet_buf[2] == 0xB2 && packet_buf[3] == 0xC1)

                {
                    j=0;
                   // rt_sem_release(trail_sem);
                    rt_event_send(strat_event, LOC(0));

                }else if (packet_buf[0] == 0xEE && packet_buf[1] == 0xEE &&
                        packet_buf[2] == 0xEE && packet_buf[3] == 0xEE)

                {

                   // rt_sem_release(trail_sem);
                    rt_event_send(strat_event, LOC(1));

                }else if(packet_buf[0] == 0xFD && packet_buf[1] == 0x88 &&
                        packet_buf[2] == 0x88 && packet_buf[3] == 0xFD)
                {
                    rt_sem_release(push_sem);
                  //  rt_kprintf("worning.......\r\n");
                }else if(packet_buf[0] == 0xAA && packet_buf[1] == 0xBB &&
                        packet_buf[2] == 0xCC && packet_buf[3] == 0xDD)
                {
                    rt_sem_release(rest_sem);
                  //  rt_kprintf("worning.......\r\n");
                }


                // 复位 + 清空缓冲区（彻底防残留）
                packet_idx = 0;
                rt_memset(packet_buf, 0, sizeof(packet_buf));
                rt_memset(buf, 0, sizeof(buf));
            }
        }
    }
}

INIT_BOARD_EXPORT(uart_Iint);
