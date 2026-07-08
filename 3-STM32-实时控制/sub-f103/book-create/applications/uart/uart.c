/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-01     17590       the first version
 */

#include <uart.h>
#include "run_framework.h"

#define PACKET_SIZE 4
static uint8_t packet_buf[PACKET_SIZE];
static int packet_idx = 0;
rt_device_t uart1_dev;
rt_device_t uart2_dev;

uint8_t buf[64];
uint8_t out[7];
rt_sem_t uart_sem;
rt_sem_t seize_open_sem;
rt_sem_t seize_open_sem1;
rt_sem_t seize_end;
rt_sem_t shut_sem;
uint8_t outpos[11];
rt_mutex_t uart3_mutex;
uint8_t state=0;

struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
struct serial_configure config2 = RT_SERIAL_CONFIG_DEFAULT;


int uart_Iint(void)
{
          uart1_dev = rt_device_find("uart1");
          uart2_dev = rt_device_find("uart2");


          uart_sem = rt_sem_create("limit1", 0, RT_IPC_FLAG_FIFO);
          seize_open_sem = rt_sem_create("limit2", 0, RT_IPC_FLAG_FIFO);
          seize_open_sem1 = rt_sem_create("limit4", 0, RT_IPC_FLAG_FIFO);
          shut_sem = rt_sem_create("limit3", 0, RT_IPC_FLAG_FIFO);
          seize_end = rt_sem_create("limit7", 0, RT_IPC_FLAG_FIFO);


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
          uart3_mutex = rt_mutex_create("uart3", RT_IPC_FLAG_PRIO);

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
          //  rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
            rt_device_write(uart2_dev,0,out,7);
            rt_thread_delay(200);
         //   rt_mutex_release(uart3_mutex);

}

void uart_IQ(void *parameted)
{
    int len, i;
    while (1)
    {
        // 等待串口接收信号
        rt_sem_take(uart_sem, RT_WAITING_FOREVER);
        // 读取串口数据
        len  = rt_device_read(uart2_dev, 0, buf, sizeof(buf));

        for (i = 0; i < len; i++)
        {
            // 1. 先找帧头，再存数据（关键！解决粘包乱码）
            if(packet_idx == 0 && buf[i] != 0xAA && buf[i] != 0xCC && buf[i] != 0xAB && buf[i] != 0xEE && buf[i] != 0xED)
            {
                continue; // 不是帧头，直接丢弃杂字节
            }

            // 存入有效字节
            packet_buf[packet_idx++] = buf[i];

            // 2. 收满一包 4 字节，开始校验
            if (packet_idx == PACKET_SIZE)
            {

                // 开门指令 AA 11 11 FF
                if(packet_buf[0]==0xAA && packet_buf[1]==0x11 && packet_buf[2]==0x11 && packet_buf[3]==0xFF)
                {
                    rt_sem_release(seize_open_sem);
                   // rt_device_write(uart2_dev, 0,open_ok, 4);
                   // rt_thread_mdelay(100);
                //    rt_kprintf("ok...\r\n");
                }
                // 关门指令 CC 33 33 FB
             //   else if(packet_buf[0]==0xCC && packet_buf[1]==0x33 && packet_buf[2]==0x33 && packet_buf[3]==0xFB)
              //  {
               //     rt_sem_release(shut_sem);
                //    rt_kprintf("worning...\r\n");
              //  }
                    else if(packet_buf[0]==0xAB && packet_buf[1]==0xA8 && packet_buf[2]==0xA8 && packet_buf[3]==0xAB)
                {
                    rt_sem_release(seize_end);
                 //   rt_kprintf("worning...\r\n");
                }
                    else if(packet_buf[0]==0xEE && packet_buf[1]==0xEE && packet_buf[2]==0xEE && packet_buf[3]==0xEE)
                                    {
                                        state=1;
                                    }
                    else if(packet_buf[0]==0xED && packet_buf[1]==0xED && packet_buf[2]==0xED && packet_buf[3]==0xED)
                                      {
                                                            state=2;
                                         }
                    else if(packet_buf[0] == 0xAA && packet_buf[1] == 0xBB &&
                                        packet_buf[2] == 0xCC && packet_buf[3] == 0xDD)
                                {

                                    state=0;
                                }

                // 无效包：不处理

                // 3. 强制复位索引 + 清空缓存
                packet_idx = 0;
                rt_memset(packet_buf, 0, sizeof(packet_buf));
                rt_memset(buf, 0, sizeof(buf));
            }
        }
    }
}

void build_pulse_rel_cmd(uint8_t addr, uint8_t dir, uint16_t speed, uint8_t acc, uint32_t rel_pulses) {
    // 速度限幅
    if (speed > 3000) speed = 3000;

    // 速度编码：高4位放在字节4的低4位，低8位放在字节5
    uint8_t speed_high = (speed >> 8) & 0x0F;   // 速度高4位（0~15）
    uint8_t speed_low  = speed & 0xFF;          // 速度低8位
    uint8_t byte3 = (dir << 7) | speed_high;    // bit7=方向，bit3-0=速度高4位

    // 指令总长：帧头+地址+功能码+字节3+速度低8位+加速度+相对脉冲数(4B)+CRC = 1+1+1+1+1+1+4+1 = 11字节
    uint8_t outpos[11];
    outpos[0] = 0xFA;
    outpos[1] = addr;
    outpos[2] = 0xFD;                  // 功能码：按脉冲数相对运动
    outpos[3] = byte3;
    outpos[4] = speed_low;
    outpos[5] = acc;

    // 相对脉冲数（大端存储，uint32_t）
    outpos[6] = (rel_pulses >> 24) & 0xFF;
    outpos[7] = (rel_pulses >> 16) & 0xFF;
    outpos[8] = (rel_pulses >> 8) & 0xFF;
    outpos[9] = rel_pulses & 0xFF;

    // 计算CHECKSUM8bit校验和
    uint8_t crc = 0;
    for (int i = 0; i < 10; i++) {
        crc += outpos[i];
    }
    outpos[10] = crc;


    rt_device_write(uart2_dev, 0, outpos, 11);
    rt_thread_delay(200);

}



INIT_BOARD_EXPORT(uart_Iint);
