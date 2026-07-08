# K230 视觉感知

两块 K230 开发板（立创庐山派）分别承担系统的两项视觉任务，均运行 RT-Smart 操作系统。

## 角色分工

| 编号 | 位置 | 摄像头 | 任务 | 通信 |
|------|------|--------|------|------|
| K230 #1 | 整书区底部 | GC2093 (MIPI CSI) | QR 码扫描 + CRC4 校验 (quirc vote 3/3) | TCP 客户端 → RK3566 :9998 |
| K230 #2 | 书架前方 | 全局快门摄像头 | AprilTag 25h9 PnP 位姿估计 + UART2 桥接 | TCP 服务端 ← RK3566 :9999 |

## 通信协议

K230 与 RK3566 之间采用文本行协议（`\n` 分隔），关键数据帧附加 CRC-4/ITU 校验。

### K230 #1 → RK3566

```
READY\n                  启动握手
BOOK:<id>:<CRC4>\n       扫到书标 QR
STOP:RETURN:<CRC4>\n     扫到停止码
```

### RK3566 → K230 #1

```
WAKE\n                   唤醒扫码
SLEEP\n                  休眠
ACK\n                    确认
NAK:<reason>\n           拒绝
```

### RK3566 → K230 #2

```
SHELVE,<y>,<x>\n         上架指令
FETCH,<y>,<x>\n          取书指令
INIT\n                   机械归位
START_RETURN\n           启动还书推书
PUSH_NEXT\n              推下一本
```

### K230 #2 → RK3566 (TCP :9997)

```
TRIGGER\n                限位触发
DONE\n                   上架完成
FETCH_DONE\n             取书完成
ERROR:<msg>\n            异常
```

## 目录说明

- `apriltag-nav/` — K230 #2 AprilTag 导航源码（源码整理中）
- `qr-scanner/` — K230 #1 QR 扫码源码（源码整理中）
- `k230_development_guide.md` — K230 开发环境搭建与固件编译指南

## 开发环境

详见 `k230_development_guide.md`，涵盖：
- Ubuntu 20.04 开发环境搭建
- K230 RT-Smart SDK 下载与编译
- 固件烧录（USB / SD 卡）
- 串口调试

## 相关文档

- 通信协议细节：`../7-项目文档/通信协议规范.md`
- 系统架构：`../7-项目文档/系统架构说明.md`
