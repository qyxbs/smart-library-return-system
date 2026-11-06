# RK3566 主控

RK3566 是系统的边缘计算中枢，运行 Linux 6.1，负责业务调度、数据管理、桥接中间件与屏幕交互。

## 目录结构

```
1-RK3566-主控/
├── cpp/                    C++ 主控工程
│   ├── src/server/         HTTP 服务与路由（book_server，:9777）
│   ├── src/bridge/         K230 桥接（:9998 / :9997 / :9999 / :9995）
│   ├── src/data/           SQLite 数据层（5 表、槽位、借阅规则、日志）
│   ├── src/core/           任务队列、串口（termios）、打印机（CRC-4 / GBK）
│   ├── src/common/         HTTP 服务器与客户端、配置、日志、JSON 工具
│   ├── tests/              单元测试与端到端测试
│   ├── static/             管理页面静态资源
│   ├── third_party/        nlohmann/json（随项目携带）
│   ├── CMakeLists.txt
│   └── toolchain-aarch64.cmake
├── voice_web_app.py        语音服务（:8890，声纹 / ASR / 分类）
├── asr_engine.py           SenseVoice ASR 封装（sherpa-onnx）
├── llm_engine.py           LLM 推理引擎（llama.cpp / DeepSeek API）
├── pipeline.py             语音识别管线：ASR → 书名提取 → 书库纠正 → 分类
├── book_index.py           本地书库索引（书名模糊匹配）
├── search_fallback.py      联网搜索兜底（书名相关性检查）
├── category_rules.py       分类关键词规则
├── analog_mic.py           模拟麦克风采集（RK809 Codec，16 kHz）
├── qr_scanner.py           OV5695 连续扫码服务（独立进程）
├── screen.html             HDMI 管理员界面（1024×600）
├── sysmon.sh               系统监视脚本（30 s 间隔，日志轮转）
├── pdm-mic-combined.dts    PDM 麦克风设备树片段（参考）
├── init-scripts/           开机自启脚本
└── deploy-scripts/         部署脚本
```

## 构建

本机（x86）：

```bash
cd cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build -j
```

交叉编译（aarch64，部署到板端）：

```bash
cmake -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=toolchain-aarch64.cmake -DCMAKE_BUILD_TYPE=Release .
cmake --build build-arm64 -j
```

依赖：CMake ≥ 3.10、支持 C++17 的编译器、SQLite3 开发库。

## 运行

```bash
./build/book_server      # HTTP 服务，:9777
./build/k230_bridge      # K230 桥接，:9998 / :9997 / :9999 / :9995
```

配置项见 `cpp/src/common/config.h`（数据库路径、端口、串口设备等）。

## 测试

```bash
cd build && ctest --output-on-failure
```

## 关键设计

1. **HTTP 服务**：每连接一个线程处理请求，路由分发；管理页面静态文件与 JSON 接口同源提供
2. **SQLite**：WAL 模式 + busy_timeout，写操作经任务队列串行
3. **任务队列**：三级优先级（借书 > 取书 > 上架）+ 单 worker 串行执行
4. **串口**：termios raw 模式，UART4（9600）/ UART5（38400）/ UART9（115200）三路
5. **打印机**：CRC-4 帧校验 + GBK 编码（iconv）
6. **K230 桥接**：TCP 长连接与短连接混合，板内 IPC 走本地 TCP
7. **摄像头**：扫码进程独占 V4L2 设备，其余进程经本地 TCP 取结果，避免设备争用

## 相关文档

- 系统架构：`../7-项目文档/系统架构说明.md`
- 通信协议：`../7-项目文档/通信协议规范.md`
- 部署指南：`../7-项目文档/部署指南.md`
