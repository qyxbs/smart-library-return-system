# STM32 实时控制

基于 RT-Thread 标准版的分布式实时控制系统，1 主控 + 3 子控制器，通过 Zigbee + RS485 协同管理 7 路步进电机。

## 拓扑结构

```
RK3566 ──UART──▶ STM32F407VET6 (主控)
                    │ CC2530 Zigbee 广播
        ┌───────────┼───────────┐
        ▼           ▼           ▼
  STM32F103C8T6  STM32F103C8T6  STM32F103C8T6
  (推书装置)      (机械爪)        (三轴运动)
```

## 工程目录

| 目录 | 芯片 | 职责 | 线程数 |
|------|------|------|--------|
| `master-f407/` | STM32F407VET6 | 主控协调：UART 协议解析、Zigbee 广播、RS485 总线管理 | 14 |
| `sub-f103/push-book/` | STM32F103C8T6 | 推书装置：步进电机控制、限位检测、电流监测 (ADC) | 6 |
| `sub-f103/book-create/` | STM32F103C8T6 | 三轴运动：运动框架 (run_framework)、限位开关、多轴协调 | 5 |

> 第三个子控制器（机械爪）的源码正在整理中。

## 应用模块

### 主控 (master-f407)

```
applications/
├── main.c           14 个控制线程入口
├── uart/            协议解析 + 电机控制指令 (24 KB .c)
├── work/            机械爪工作流程编排
└── limite/          限位开关检测
```

### 推书装置 (push-book)

```
applications/
├── main.c           6 个线程入口
├── uart/            UART 通信 (6.6 KB .c)
├── ADC/             电流监测 (4.7 KB .c)
└── limite/          限位检测 (5.1 KB .c)
```

### 三轴运动 (book-create)

```
applications/
├── main.c           5 个线程入口
├── uart/            UART 通信 (7.2 KB .c)
├── run_framework/   多轴运动框架 (5.1 KB .c)
├── limit_switch/    限位开关检测 (4.4 KB .c)
└── adc/             模数转换
```

## 编译与烧录

### 环境要求

- RT-Thread Studio 或 RT-Thread env 工具
- ARM GCC 工具链 (arm-none-eabi-gcc)
- ST-Link 烧录器 (SWD 接口)

### 编译

```bash
# 在 RT-Thread env 中进入工程目录
cd master-f407/
scons -j$(nproc)

cd sub-f103/push-book/
scons -j$(nproc)

cd sub-f103/book-create/
scons -j$(nproc)
```

### 烧录

通过 ST-Link (SWD) 分别烧录各芯片的 `rtthread.bin`。

## 通信

- **RK3566 → STM32**: UART 无线串口 (9600bps 坐标 + 38400bps 控制)
- **STM32F407 → STM32F103**: CC2530 Zigbee 无线串口透传广播
- **子控 → 电机**: RS485 总线 (步进电机驱动)

## 相关文档

- 通信协议细节：`../7-项目文档/通信协议规范.md`
- 系统架构：`../7-项目文档/系统架构说明.md`
