# RK3566 主控

RK3566 是系统的边缘计算中枢，运行 Linux 6.1，负责业务调度、AI 推理、桥接中间件与数据库管理。

## 文件说明

### 核心应用 (`/userdata/book_app/`)

| 文件 | 大小 | 说明 |
|------|------|------|
| `app.py` | 57 KB | Flask API 主程序，30+ 端点，含鉴权、UART 控制、任务管理 |
| `db.py` | 29 KB | SQLite 数据库层，5 表 ORM，含槽位分配、借阅规则、罚款计算 |
| `k230_bridge_v2.py` | 61 KB | K230 双向桥接中间件，TCP :9998/:9999 长连接管理，还书状态机 |
| `tasks.py` | 12 KB | 机械爪任务队列（BORROW > RETRIEVE > SHELVE 三级优先级） |
| `qr_scanner.py` | 10 KB | OV5695 连续扫码服务（V4L2 BUFFERSIZE=4），CRC4 校验 |
| `screen.html` | 64 KB | HDMI 管理员触屏界面（1024×600），声纹登录 + 6 功能页 |
| `print_label.py` | 7 KB | EM20L 热敏打印机标签生成（50×50mm，QR + 书名） |
| `monitor_api.py` | 5.7 KB | 系统监视 API（CPU/内存/WiFi/温度） |
| `sysmon.sh` | 2.4 KB | 系统监视 Shell 脚本，30s 间隔，1MB 轮转日志 |

### AI 引擎

| 文件 | 说明 |
|------|------|
| `voice_web_app.py` | 语音 AI Web 服务 (:8890)：声纹验证 + ASR + LLM 分类 + 管线调度 |
| `asr_engine.py` | SenseVoice ASR 引擎封装（sherpa-onnx） |
| `llm_engine.py` | LLM 推理引擎（llama.cpp / DeepSeek API 双引擎 failover） |
| `pipeline.py` | 语音识别管线：ASR → LLM 提取 → 书库纠正 → 分类输出 |
| `book_index.py` | 本地书库索引（414 万条书名模糊匹配） |
| `search_fallback.py` | Serper 联网搜索兜底（书名相关性检查） |
| `category_rules.py` | 分类关键词规则（LLM 误分类纠正） |
| `analog_mic.py` | 模拟麦克风录音（RK809 Codec, S32_LE, 16kHz） |

### 部署脚本 (`deploy-scripts/`)

| 文件 | 说明 |
|------|------|
| `deploy_full.sh` | 全量部署 |
| `deploy_20260702.sh` | 增量部署（指定日期版本） |
| `deploy_label.sh` | 打印标签部署 |

### 开机自启 (`init-scripts/`)

| 文件 | 说明 |
|------|------|
| `S98wifi` | WiFi STA 连接管理 |
| `S99book` | Flask API :9777 |
| `S99a-voice` | 语音 AI 服务 :8890 |
| `S99sysmon` | 系统监视 |

### 设备树

| 文件 | 说明 |
|------|------|
| `pdm-mic-combined.dts` | PDM 麦克风设备树片段（参考） |

## 依赖

```
flask opencv-python pyserial sherpa-onnx pypinyin
```

## 关键设计决策

1. **UART 直控** — 所有 STM32 通信通过 UART 直接发送，不经过 MQTT/云端中转
2. **双引擎 AI** — 有网时优先 DeepSeek API，断网 <300ms 切换本地 Qwen3-0.6B
3. **V4L2 BUFFERSIZE** — 必须 ≥4，否则连续扫码失败
4. **SIGTERM 而非 fuser -k** — 正常释放 V4L2 资源，避免摄像头驱动状态损坏
5. **USB 有源 Hub 供电必须接入** — WiFi TX 高负载时电压跌落会导致 USB 总线挂死

## 相关文档

- 系统架构：`../7-项目文档/系统架构说明.md`
- 通信协议：`../7-项目文档/通信协议规范.md`
- 部署指南：`../7-项目文档/部署指南.md`
