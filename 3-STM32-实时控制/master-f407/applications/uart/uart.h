/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-16     17590       the first version
 */
#ifndef APPLICATIONS_UART_UART_H_
#define APPLICATIONS_UART_UART_H_


#include <board.h>
#include <rtdevice.h>
#include <rtdbg.h>
#include <rtthread.h>
#include "errno.h"


extern rt_device_t uart1_dev;
extern rt_device_t uart3_dev;
/*

extern  uint8_t dir0[7];
extern   uint8_t dir1[7];


extern  uint8_t dir0_six[7];
extern   uint8_t dir1_six[7];
*/
extern  uint8_t spot_six[7];
extern  uint8_t spot[7];
extern uint8_t contant[6];
extern rt_mutex_t uart3_mutex;
extern rt_mutex_t uart2_mutex;
void build_rel_pulse_cmd(uint8_t dir,uint16_t speed_rpm,uint32_t pulses);
void build_speed_cmd(uint8_t addr, uint8_t dir, uint16_t speed, uint8_t acc);
extern uint8_t buf[4];
void uart_IQ(void *parameted);
extern rt_sem_t uart_sem;
extern rt_sem_t open_close;
extern rt_sem_t back_sem;
void Message_Process(uint8_t *buf, uint8_t len);
extern rt_sem_t send_sem;
extern uint8_t out[7];
void uart2_IQ(void *parameter);
void build_speed_stop(uint8_t addr, uint8_t acc);
extern uint8_t out_spot[7];
extern rt_device_t uart2_dev;
extern uint8_t contant[6];
void abscissa(void *parameter);
void ordinate(void *parameter);
extern rt_sem_t x_sem;
void build_pulse_rel_cmd(uint8_t addr, uint8_t dir, uint16_t speed, uint8_t acc, uint32_t rel_pulses);
extern rt_sem_t y_sem;
extern rt_sem_t next_sem;
extern  uint8_t m,q;
extern uint8_t outpos[11];
extern uint8_t aim_x;
extern uint8_t scan2[2];
extern uint8_t x,y,z,state;
extern uint8_t action;
extern rt_sem_t get_sem;
extern rt_sem_t get_next_sem;
extern rt_sem_t get_end_sem;
extern uint8_t end[4];
extern uint32_t  pulses;
void scan_tid(void);
extern rt_sem_t start_sem;
#endif /* APPLICATIONS_UART_UART_H_ */
