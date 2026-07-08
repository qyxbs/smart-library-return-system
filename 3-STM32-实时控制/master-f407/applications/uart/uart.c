/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-16     17590       the first version
 */
  #include <uart.h>
#define PACKET_SIZE 4
static uint8_t packet_buf[PACKET_SIZE];
static int packet_idx = 0;
rt_device_t uart1_dev;
rt_device_t uart2_dev;
rt_device_t uart3_dev;
uint8_t buf[4];
static uint8_t l=0;
rt_err_t ret;
rt_mutex_t coord_mutex;
rt_mutex_t state_mutex;
struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
struct serial_configure config2 = RT_SERIAL_CONFIG_DEFAULT;
struct serial_configure config3 = RT_SERIAL_CONFIG_DEFAULT;
uint8_t spot[7] ={0xFA,0x04,0xF6,0x00,0x00,0x00,0xF4};
uint8_t spot_six[7] ={0xFA,0x06,0xF6,0x00,0x00,0x00,0xF6};
#define UART_RX_BUF_MAX_LEN   32
uint8_t rx_buf[UART_RX_BUF_MAX_LEN];
uint8_t rx_cnt = 0;
uint8_t action=0;
rt_mutex_t uart3_mutex;
rt_mutex_t uart2_mutex;
uint32_t  pulses;
uint8_t local_aim_x;
uint8_t scan2[2]={0xBA,0XBA};
//uint8_t a=0x00;
//uint8_t a=0xee;
/*

uint8_t dir0[7]={0xFA,0x04,0xF6,0x00,0x40,0x02,0x36};//顺时针，前进，上升
uint8_t dir1[7]={0xFA,0x04,0xF6,0x80,0x40,0x02,0xB6};//逆时针，后退，下降


uint8_t dir0_six[7]={0xFA,0x06,0xF6,0x00,0x40,0x02,0x38};//顺时针，前进，上升
uint8_t dir1_six[7]={0xFA,0x06,0xF6,0x80,0x40,0x02,0xB8};
*/
uint8_t out[7];
uint8_t out_spot[7];
static uint8_t  buffer[11];
uint8_t end[4]={0xAB,0xA8,0xA8,0xAB};

uint8_t curr_y = 0;
 uint8_t curr_x = 0;
 // 目标坐标：来自单字节数据
 uint8_t aim_y  =  (0x16 >> 4) & 0x0F;
 uint8_t aim_x  =   0x16 & 0x0F;



 // Y=1运动信号量
 /* 串口接收缓冲区 */
 //static uint8_t rx_buf[2];
 //static uint8_t rx_cnt = 0;
 uint8_t z=0,e=0,q=0,m=0,a=0xff;
 uint8_t outpos[11];
uint8_t contant[6]={0x04,0xE4,0x04,0x00,0xFF,0x14};//x04\xE4\x04\x00\xFF\x14
rt_sem_t uart_sem;
rt_sem_t uart2_sem;
rt_sem_t open_close;
rt_sem_t back_sem;
rt_sem_t send_sem;
rt_sem_t get_sem;
rt_sem_t get_end_sem;
rt_sem_t get_next_sem;
uint8_t state=0;
//rt_sem_t limite_sem;
rt_sem_t x_sem;
uint8_t x=0,y=0;
rt_sem_t next_sem;
rt_sem_t y_sem;
rt_sem_t start_sem;
uint8_t rest_start[4]={0XEE,0XEE,0XEE,0XEE};
void Parse_UART_Frame(uint8_t *buf, uint8_t len);
void build_pulse_rel_cmd(uint8_t addr, uint8_t dir, uint16_t speed, uint8_t acc, uint32_t rel_pulses);

void Message_Process(uint8_t *buf, uint8_t len);
rt_err_t uart_input(rt_device_t dev, rt_size_t size)
{

    rt_sem_release(uart_sem);
    rt_sem_release(uart2_sem);
    // rt_device_write(uart3_dev, 0,&a, 1);
                   //     rt_thread_delay(200);
   // rt_device_write(uart3_dev,0,contant,6);
   // rt_kprintf("input...");
   // rt_kprintf("fail\n");


    return 0;
}
int uart_Iint(void)
{
    /*
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;   // 开启GPIOA时钟
       RCC->APB1ENR |= RCC_APB1ENR_USART2EN;  // 开启USART2时钟

       // 2. 配置PA0、PA1为复用功能模式
       GPIOA->MODER &= ~(GPIO_MODER_MODE0 | GPIO_MODER_MODE1);
       GPIOA->MODER |= (2 << 0*2) | (2 << 1*2);  // 模式2：复用功能模式

       // 3. 配置引脚为推挽输出、上拉、高速
       GPIOA->OTYPER &= ~(GPIO_OTYPER_OT0 | GPIO_OTYPER_OT1); // 推挽输出
       GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD0 | GPIO_PUPDR_PUPD1);
       GPIOA->PUPDR |= (1 << 0*2) | (1 << 1*2);  // 上拉
       GPIOA->OSPEEDR |= (3 << 0*2) | (3 << 1*2); // 高速模式

       // 4. 关键！配置复用功能AF1（USART2对应的AF编号）
       // AFR[0]控制引脚0-7，AF1对应USART2
       GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL0 | GPIO_AFRL_AFSEL1);
       GPIOA->AFR[0] |= (1 << 0*4) | (1 << 1*4);  // 引脚0、1复用为AF1
*/

          uart1_dev = rt_device_find("uart1");
          uart2_dev = rt_device_find("uart2");
          uart3_dev = rt_device_find("uart3");
          config.baud_rate = BAUD_RATE_38400;
          config.data_bits = DATA_BITS_8;
          config.stop_bits = STOP_BITS_1;
          config.parity    = PARITY_NONE;

          config2.baud_rate = BAUD_RATE_9600;
          config2.data_bits = DATA_BITS_8;
          config2.stop_bits = STOP_BITS_1;
          config2.parity    = PARITY_NONE;

          config3.baud_rate = BAUD_RATE_38400;
          config3.data_bits = DATA_BITS_8;
          config3.stop_bits = STOP_BITS_1;
          config3.parity    = PARITY_NONE;

          rt_device_open(uart1_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
          rt_device_open(uart2_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
          rt_device_open(uart3_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);

          rt_device_control(uart1_dev, RT_DEVICE_CTRL_CONFIG, &config);
          rt_device_control(uart2_dev, RT_DEVICE_CTRL_CONFIG, &config2);
          rt_device_control(uart3_dev, RT_DEVICE_CTRL_CONFIG, &config3);




 rt_device_set_rx_indicate(uart3_dev, uart_input);
 rt_device_set_rx_indicate(uart2_dev, uart_input);
          uart_sem = rt_sem_create("limit1", 0, RT_IPC_FLAG_FIFO);
          next_sem = rt_sem_create("limit051", 0, RT_IPC_FLAG_FIFO);
          uart2_sem = rt_sem_create("limit5", 0, RT_IPC_FLAG_FIFO);
          open_close = rt_sem_create("limit2", 0, RT_IPC_FLAG_FIFO);
          back_sem = rt_sem_create("limit3", 0, RT_IPC_FLAG_FIFO);
          send_sem = rt_sem_create("limit4", 0, RT_IPC_FLAG_FIFO);
          x_sem = rt_sem_create("limit5", 0, RT_IPC_FLAG_FIFO);
          y_sem = rt_sem_create("limit6", 0, RT_IPC_FLAG_FIFO);
          get_sem = rt_sem_create("limit11", 0, RT_IPC_FLAG_FIFO);
          get_end_sem = rt_sem_create("limit31", 0, RT_IPC_FLAG_FIFO);
          get_next_sem = rt_sem_create("limit21", 0, RT_IPC_FLAG_FIFO);
        //  limite_sem = rt_sem_create("limit10", 0, RT_IPC_FLAG_FIFO);
          start_sem = rt_sem_create("limit7", 0, RT_IPC_FLAG_FIFO);
        //  rt_device_read(uart1_dev, 0, buf, sizeof(buf));
          uart3_mutex = rt_mutex_create("uart3", RT_IPC_FLAG_PRIO);
          uart2_mutex = rt_mutex_create("uart2", RT_IPC_FLAG_PRIO);
          coord_mutex = rt_mutex_create("coord", RT_IPC_FLAG_PRIO);
              state_mutex = rt_mutex_create("state", RT_IPC_FLAG_PRIO);
          return 0;
}

void build_rel_pulse_cmd(uint8_t dir,uint16_t speed_rpm,uint32_t pulses)
{




    uint8_t speed_high = (speed_rpm >> 8) & 0x0F;   // 取高4位
    uint8_t speed_low  = speed_rpm & 0xFF;         // 取低8位

    // 字节4：最高位为方向，低4位为速度高4位
    uint8_t dir_speed = (dir << 7) | speed_high;

    // 构建指令帧（大端序）
    buffer[0] = 0xFA;                // 帧头
    buffer[1] = 0x66;           // 分组地址
    buffer[2] = 0xFD;                 // 功能码：脉冲相对运动
    buffer[3] = dir_speed;            // 方向 + 速度高4位
    buffer[4] = speed_low;            // 速度低8位
    buffer[5] = 0x02;                  // 加速度
    buffer[6] = (pulses >> 24) & 0xFF; // 脉冲数最高8位
    buffer[7] = (pulses >> 16) & 0xFF;
    buffer[8] = (pulses >> 8) & 0xFF;
    buffer[9] = pulses & 0xFF;         // 脉冲数最低8位

    // 计算校验和（CHECKSUM8bit）：前10个字节累加，取低8位
    uint8_t crc = 0;
    for (int i = 0; i < 10; i++) {
        crc += buffer[i];
    }
    buffer[10] = crc;                  // 第11字节为CRC
   // rt_thread_mdelay(100);
    rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
      rt_device_write(uart3_dev, 0,buffer, 11);
            rt_mutex_release(uart3_mutex);

   // rt_thread_mdelay(100);

}


void uart_IQ(void *parameted)
{
    int len, i;
    while (1)
    {
        rt_sem_take(uart_sem, RT_WAITING_FOREVER);

        len = rt_device_read(uart3_dev, 0, buf, sizeof(buf));

        for (i = 0; i < len; i++)
        {
            // 关键：不是合法帧头直接丢弃，避免杂数据堆积乱包
            if (packet_idx == 0 && buf[i] != 0xBB && buf[i] != 0xDD && buf[i] != 0xFD && buf[i] != 0xCB && buf[i] != 0xB3 && buf[i] != 0xCA && buf[i] != 0xAC && buf[i] != 0xED && buf[i] != 0xAA && buf[i] != 0xDA)
            {

                continue;
            }

            packet_buf[packet_idx++] = buf[i];

            // 收满一包
            if (packet_idx == PACKET_SIZE)
            {


                // 开门
                if(packet_buf[0] == 0xBB && packet_buf[1] == 0x22 &&
                   packet_buf[2] == 0x22 && packet_buf[3] == 0xFA)
                {
                    rt_sem_release(open_close);
                   // rt_device_write(uart3_dev,0,contant,6);
                  //  rt_kprintf("ok...\r\n");
                }
                // 回位/报警
                else if(packet_buf[0] == 0xDD && packet_buf[1] == 0x44 &&
                        packet_buf[2] == 0x44 && packet_buf[3] == 0xFC)
                {
                    if(state==1){

                        rt_sem_release(back_sem);
                    }else if (state==2) {
                        rt_sem_release(get_next_sem);
                    }

                  //  rt_kprintf("worning.......\r\n");
                }
                else if(packet_buf[0] == 0xFD && packet_buf[1] == 0x88 &&
                                        packet_buf[2] == 0x88 && packet_buf[3] == 0xFD)
                                {
                                    rt_sem_release(start_sem);
                                    state=1;
                                  //  rt_kprintf("worning.......\r\n");
                                }
                else if(packet_buf[0] == 0xCB && packet_buf[1] == 0xCA &&
                                                        packet_buf[2] == 0xCA && packet_buf[3] == 0xCB)
                              {
                                   rt_device_write(uart3_dev,0,scan2,2);
                                   rt_thread_delay(200);
                              }else if(packet_buf[0] == 0xB3 && packet_buf[1] == 0xA1 &&
                                      packet_buf[2] == 0xB2 && packet_buf[3] == 0xC1)
            {
                                  l++;
                                  if (l>1) {
                                    rt_device_write(uart3_dev,0,rest_start,4);
                                    rt_thread_delay(200);
                                }

            }else if(packet_buf[0] == 0xCA && packet_buf[1] == 0xCA &&
                    packet_buf[2] == 0xCA && packet_buf[3] == 0xCA)
            {
                rt_sem_release(y_sem);
              //  rt_kprintf("worning.......\r\n");
            }else if(packet_buf[0] == 0xAC && packet_buf[1] == 0xAC &&
                    packet_buf[2] == 0xAC && packet_buf[3] == 0xAC)
            {
                rt_sem_release(x_sem);
              //  rt_kprintf("worning.......\r\n");
            }else if(packet_buf[0] == 0xED && packet_buf[1] == 0xED &&
                    packet_buf[2] == 0xED && packet_buf[3] == 0xED)
            {
                rt_sem_release(get_sem);
                state=2;
            }else if(packet_buf[0] == 0xAA && packet_buf[1] == 0xBB &&
                    packet_buf[2] == 0xCC && packet_buf[3] == 0xDD)
            {

                state=0;
            }else if(packet_buf[0] == 0xDA && packet_buf[1] == 0xDA &&
                    packet_buf[2] == 0xDA && packet_buf[3] == 0xDA)
            {
                rt_sem_release(get_next_sem);

            }


                // 复位 + 清空缓冲区（彻底防残留）
                packet_idx = 0;
                rt_memset(packet_buf, 0, sizeof(packet_buf));
                rt_memset(buf, 0, sizeof(buf));
            }
        }
    }
}









void uart2_IQ(void *parameter)
{
    uint8_t ch;

    while(1)
    {
        rt_sem_take(uart2_sem, RT_WAITING_FOREVER);
      //  rt_thread_delay(200);
       //    rt_device_write(uart3_dev, 0,&a, 1);
        //   rt_thread_delay(200);
       // rt_device_write(uart3_dev,0,&a,1);
          //rt_thread_delay(200);
        while(rt_device_read(uart2_dev, 0, &ch, 1) == 1)
        {
            if(rx_cnt < UART_RX_BUF_MAX_LEN)
            {
                rx_buf[rx_cnt++] = ch;
            }
         //   rt_device_write(uart3_dev, 0,rx_buf, 1);
            // 优先判断：是不是 FA 开头的2字节短帧
            if(rx_cnt == 2 && rx_buf[0] == 0xFA)
            {
                Message_Process(rx_buf, 2);
                rx_cnt = 0;
                rt_memset(rx_buf, 0, sizeof(rx_buf));
            }
            // 判断：长报文结束符0D，走协议解析
            else if(ch == 0x0D)
            {
                Parse_UART_Frame(rx_buf, rx_cnt);
                rx_cnt = 0;

             //   rt_device_write(uart3_dev, 0,&a, 1);
                rt_memset(rx_buf, 0, sizeof(rx_buf));
            }
        }
    }
}


void Parse_UART_Frame(uint8_t *buf, uint8_t len)
{
    int i = 0;
    uint8_t ascii1 = 0, ascii2 = 0;
    uint8_t valid_data = 0;
    int find_flag = 0;

    // 找 0x2C 逗号后面两个ASCII数字
    for(i = 0; i < len - 2; i++)
    {
        if(buf[i] == 0x2C)
        {
            ascii1 = buf[i+1];
            ascii2 = buf[i+2];
            find_flag = 1;
            break;
        }
    }

    if(1 != find_flag)
    {
        return;
    }

    // ASCII转数值
    uint8_t num1 = ascii1 - 0x30;
    uint8_t num2 = ascii2 - 0x30;
    valid_data = (num1 << 4) | num2;

  //  rt_device_write(uart3_dev, 0,&valid_data, 1);
  //  rt_thread_delay(200);
    // 单字节送入处理（目标坐标）
    Message_Process(&valid_data, 1);
}

void Message_Process(uint8_t *buf, uint8_t len)
{

    // 情况1：收到 2字节 帧头FA + 坐标 【现在改为：赋值目标坐标aim】
    if(len == 2 && buf[0] == 0xFA)
    {
        uint8_t data = buf[1];
        rt_mutex_take(coord_mutex, RT_WAITING_FOREVER);
        aim_y = (data >> 4) & 0x0F;
        aim_x = data & 0x0F;
        rt_mutex_release(coord_mutex);
        //rt_mutex_take(coord_mutex, RT_WAITING_FOREVER);
                  //   local_aim_x = aim_x;
         //            rt_mutex_release(coord_mutex);

                   //         rt_device_write(uart3_dev,0,&a,1);
                    //         rt_thread_delay(200);
     //   rt_sem_release(y_sem);
    }
    // 情况2：单字节 【长报文解析出来的目标坐标】
    else if(len == 1)
    {
        uint8_t data = buf[0];
        rt_mutex_take(coord_mutex, RT_WAITING_FOREVER);
        curr_y = (data >> 4) & 0x0F;
        curr_x = data & 0x0F;
        rt_mutex_release(coord_mutex);
    }

    // ========== 你原有核心判断逻辑完全不动 ==========
/*
    if( curr_x == aim_x && curr_y == aim_y)
       {
           if(e==0)
           {
               rt_mutex_take(state_mutex, RT_WAITING_FOREVER);
                        //  z=0;
                        m++;
                        e++;
                rt_mutex_release(state_mutex);

               rt_sem_release(x_sem);



           }

       }
       */
    if(curr_y == aim_y )
    {
        if(z==0)
        {
            rt_mutex_take(state_mutex, RT_WAITING_FOREVER);
            action=1;
            z++;
            rt_mutex_release(state_mutex);


            rt_sem_release(y_sem);

        }
    }
/*
     if( curr_x != aim_x  || curr_y != aim_y)
    {
         if(m==0)
         {
            // rt_thread_delay(1000);
             for(q=0;q<6;q++)
         {
                 rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
                     rt_device_write(uart2_dev,0,contant,6);
                     rt_thread_delay(300);
                     rt_mutex_release(uart3_mutex);


         }

         }



    //    rt_thread_delay(200);
    }
     */
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
    rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
    rt_device_write(uart3_dev,0,out,7);
    rt_thread_delay(200);
    rt_mutex_release(uart3_mutex);


}
void build_speed_stop(uint8_t addr, uint8_t acc) {
    out_spot[0] = 0xFA;
    out_spot[1] = addr;
    out_spot[2] = 0xF6;
    out_spot[3] = 0x00;
    out_spot[4] = 0x00;
    out_spot[5] = acc;

    uint8_t crc = 0;
    for (int i = 0; i < 6; i++) {
        crc += out[i];
    }
    out_spot[6] = crc;
    rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
    rt_device_write(uart3_dev,0,out_spot,7);
       rt_thread_delay(200);
    rt_mutex_release(uart3_mutex);


}

void abscissa(void *parameter)
{
    while(1)
    {
        rt_sem_take(x_sem, RT_WAITING_FOREVER);
       //  rt_mutex_take(state_mutex, RT_WAITING_FOREVER);
          //  z=0;
       //   m++;
        //  e++;
        //  rt_mutex_release(state_mutex);
if (x==0)
{
    x++;

        rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
        rt_device_write(uart3_dev,0,spot_six,7);
        rt_thread_delay(200);
        rt_device_write(uart3_dev,0,spot_six,7);
        rt_thread_delay(200);
        rt_device_write(uart3_dev,0,spot_six,7);
        rt_thread_delay(200);

        rt_mutex_release(uart3_mutex);

               build_pulse_rel_cmd(0x04,1,750,50,280000);
               build_pulse_rel_cmd(0x04,1,750,50,280000);
               build_pulse_rel_cmd(0x04,1,750,50,280000);
               rt_thread_delay(12000);

               if(state==1)
               {
               build_rel_pulse_cmd(1,500,82000);
               build_rel_pulse_cmd(1,500,82000);
               build_rel_pulse_cmd(1,500,82000);
               rt_thread_delay(5000);
               rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
               rt_device_write(uart3_dev,0,end,4);
               rt_thread_delay(200);
               rt_mutex_release(uart3_mutex);
            //   rt_device_write(uart3_dev,0,end,4);
            //   rt_thread_delay(200);
            //   rt_device_write(uart3_dev,0,end,4);
               rt_thread_delay(5000);
               z=0;
               action=0;
               rt_sem_release(next_sem);
               }
               else if (state==2)
               {
                   build_speed_cmd(0x66,1,500,2);
                                                rt_thread_delay(200);
                                 build_speed_cmd(0x66,1,500,2);
                                           rt_thread_delay(200);

               }

       //   rt_device_write(uart3_dev,0,spot_six,7);
        //  rt_thread_delay(200);

}


    }



}

void ordinate(void *parameter)
{
    while(1)
    {
        rt_sem_take(y_sem, RT_WAITING_FOREVER);
if (y==0) {

        y++;
        rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
        rt_device_write(uart3_dev,0,spot,7);
        rt_thread_delay(200);
        rt_device_write(uart3_dev,0,spot,7);
         rt_thread_delay(200);
         rt_device_write(uart3_dev,0,spot,7);
         rt_thread_delay(200);
         build_speed_cmd(0x06,1,500,2);//6号，1左，0右。背对机器
         rt_thread_delay(200);
         build_speed_cmd(0x06,1,500,2);//6号，1左，0右。背对机器
         rt_thread_delay(200);
         build_speed_cmd(0x06,1,500,2);//6号，1左，0右。背对机器
         rt_thread_delay(200);

        rt_mutex_release(uart3_mutex);

}
           //  rt_mutex_take(coord_mutex, RT_WAITING_FOREVER);
           //   local_aim_x = aim_x;
          //   rt_mutex_release(coord_mutex);

            //  rt_device_write(uart3_dev,0,&local_aim_x,1);
           //   rt_thread_delay(200);
/*
        if (local_aim_x >= 1 && local_aim_x <= 4)
        {
//aim_x
        //    rt_device_write(uart3_dev,0,&local_aim_x,1);
        //   rt_thread_delay(200);

             pulses =10000+(local_aim_x-1)  * 14000;
             build_pulse_rel_cmd(0x06,1,200,50,pulses);
             build_pulse_rel_cmd(0x06,1,200,50,pulses);
             build_pulse_rel_cmd(0x06,1,200,50,pulses);
           //  rt_device_write(uart3_dev,0,&pulses,4);
           //              rt_thread_delay(200);
        }

        if (local_aim_x >= 5 && local_aim_x <= 8)
        {
           // rt_device_write(uart3_dev,0,&local_aim_x,1);
          //  rt_thread_delay(200);
                    pulses = 91000+(local_aim_x-5) * 14000;
                    build_pulse_rel_cmd(0x06,1,200,50,pulses);
                    build_pulse_rel_cmd(0x06,1,200,50,pulses);
                    build_pulse_rel_cmd(0x06,1,200,50,pulses);
         }

        rt_thread_delay(2000);
        build_pulse_rel_cmd(0x04,1,500,50,370000);
        build_pulse_rel_cmd(0x04,1,500,50,370000);
        build_pulse_rel_cmd(0x04,1,500,50,370000);
        rt_thread_delay(12000);
        build_rel_pulse_cmd(1,200,86000);
        build_rel_pulse_cmd(1,200,86000);
        build_rel_pulse_cmd(1,200,86000);
        rt_thread_delay(10000);
        rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
        rt_device_write(uart3_dev,0,end,4);
        rt_thread_delay(200);
        rt_mutex_release(uart3_mutex);
        rt_thread_delay(5000);
        z=0;
        action=0;
        rt_sem_release(next_sem);
*/
    }
}
void scan_tid(void)
{
    while(1)
    {
        rt_thread_delay(1000);
       // rt_device_write(uart3_dev, 0,&a, 1);
         //           rt_thread_delay(200);
        if(action==0)
        {
             for(q=0;q<12;q++)
             {
                 rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
                     rt_device_write(uart2_dev,0,contant,6);
                     rt_mutex_release(uart3_mutex);
                     rt_thread_delay(200);
             }
        }else {
           // action=0;
            break;
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

    rt_mutex_take(uart3_mutex, RT_WAITING_FOREVER);
    rt_device_write(uart3_dev, 0, outpos, 11);
    rt_thread_delay(200);
    rt_mutex_release(uart3_mutex);

}

INIT_BOARD_EXPORT(uart_Iint);



