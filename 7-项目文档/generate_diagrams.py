#!/usr/bin/env python3
"""
技术报告框图生成器 v2 — 评委视角
纯 Python stdlib，输出高质量矢量 SVG

设计原则:
  - 展现架构思想，而非操作细节
  - 突出创新点: 三芯异构、视觉闭环、双引擎 failover
  - RT-Thread 跨芯片覆盖是核心竞争力
"""

import xml.etree.ElementTree as ET
import os

OUT_DIR = "/mnt/c/Users/qybs/Desktop"

# ──── SVG 辅助函数 (同 v1) ────────────────────────────────────
SVG_NS = "http://www.w3.org/2000/svg"

def svg_elem(tag, **attrs):
    e = ET.Element(tag)
    for k, v in attrs.items():
        e.set(k.replace("_", "-"), str(v))
    return e

def text(parent, x, y, s, size=13, bold=False, anchor="middle",
         fill="#222", font="Microsoft YaHei, SimHei, sans-serif"):
    t = svg_elem("text", x=x, y=y, font_family=font, font_size=size,
                  text_anchor=anchor, fill=fill)
    if bold:
        t.set("font-weight", "bold")
    t.text = s
    parent.append(t)

def rect(parent, x, y, w, h, rx=4, fill="#fff", stroke="#333", stroke_width="1.5"):
    parent.append(svg_elem("rect", x=x, y=y, width=w, height=h, rx=rx,
                           fill=fill, stroke=stroke, stroke_width=stroke_width))

def arrow_line(parent, x1, y1, x2, y2, stroke="#555", stroke_width="1.5"):
    """Line with arrowhead at end"""
    parent.append(svg_elem("line", x1=x1, y1=y1, x2=x2, y2=y2,
                           stroke=stroke, stroke_width=stroke_width))
    dx, dy = x2 - x1, y2 - y1
    length = (dx*dx + dy*dy) ** 0.5
    if length > 0:
        ux, uy = dx/length, dy/length
        px, py = -uy, ux
        s = 7
        points = f"{x2},{y2} {x2-ux*s+px*3},{y2-uy*s+py*3} {x2-ux*s-px*3},{y2-uy*s-py*3}"
        parent.append(svg_elem("polygon", points=points, fill=stroke))

def line(parent, x1, y1, x2, y2, stroke="#555", stroke_width="1.5", dash=None):
    attrs = {"x1": x1, "y1": y1, "x2": x2, "y2": y2,
             "stroke": stroke, "stroke_width": stroke_width}
    if dash:
        attrs["stroke_dasharray"] = dash
    parent.append(svg_elem("line", **attrs))

def dashed_rect(parent, x, y, w, h, rx=4, fill="#fafafa", stroke="#aaa"):
    """Dashed border box"""
    parent.append(svg_elem("rect", x=x, y=y, width=w, height=h, rx=rx,
                           fill=fill, stroke=stroke, stroke_width="1.5",
                           stroke_dasharray="6,3"))

def title_bar(parent, x, y, w, h, label, fill="#4a7db5", text_color="#fff", size=12):
    """Colored title bar with rounded top corners"""
    rect(parent, x, y, w, h, rx=6, fill=fill, stroke="none")
    parent.append(svg_elem("rect", x=x, y=y+h-6, width=w, height=8,
                           fill=fill, stroke="none"))
    text(parent, x + w/2, y + h/2 + 5, label, size=size, bold=True, fill=text_color)

def content_box(parent, x, y, w, title, lines, title_fill="#2c5f8a",
                body_fill="#e3edf8", body_stroke="#2c5f8a",
                title_h=28, line_h=18, title_size=12, line_size=10):
    """Box with title bar + content lines. Returns total height."""
    n = len(lines)
    total_h = title_h + n * line_h + 10
    # Body
    rect(parent, x, y, w, total_h, rx=4, fill=body_fill, stroke=body_stroke)
    # Title
    title_bar(parent, x, y, w, title_h, title, fill=title_fill, size=title_size)
    # Content
    for i, line in enumerate(lines):
        if line == "---":
            continue  # skip separator lines
        fill_c = "#444"
        is_label = line.startswith("★")
        if is_label:
            fill_c = "#c0392b"
        text(parent, x + w/2, y + title_h + 12 + i * line_h, line,
             size=line_size, fill=fill_c)
    return total_h

# ═══════════════════════════════════════════════════════════════
# 图1: 边端协同计算拓扑
# ═══════════════════════════════════════════════════════════════

def draw_edge_computing_topology():
    W, H = 1000, 540
    svg = ET.Element("svg", xmlns=SVG_NS, viewBox=f"0 0 {W} {H}",
                     width=str(W), height=str(H))
    svg.append(svg_elem("rect", width="100%", height="100%", fill="#fff"))
    text(svg, 500, 26, "图2-1  系统整体拓扑", size=14, bold=True)

    # ═══════════ 坐标规划 (所有 y 值集中定义，严格避开重叠) ═══════════
    # Cloud:    48-78
    # K230:     98-158  (WiFi 客户端，在 WiFi 框上部)
    # RK3566:   175-255 (WiFi 框中心)
    # Periph:   280-350 (外设)
    # SubCtrl:  378-433 (子控制器，Zigbee 下挂)
    # User:     468-500 (用户终端)
    # WiFi box: 90-260 (覆盖 K230 + RK3566)

    # ── 云端 AI ──
    dashed_rect(svg, 300, 48, 400, 32, fill="#fef9e7")
    text(svg, 500, 62, "云端 AI  DeepSeek v4-pro", size=11, bold=True, fill="#b7950b")
    text(svg, 500, 74, "断网自动切换 Qwen3-0.6B 端侧推理", size=9, fill="#8b7300")
    arrow_line(svg, 500, 80, 500, 90, stroke="#b7950b")

    # ── WiFi 局域网络 (虚线大框，包含 K230 + RK3566) ──
    dashed_rect(svg, 25, 90, 950, 172, fill="#fafcfd", stroke="#7fb3d8")
    text(svg, 500, 103, "2.4GHz WiFi 局域网  ·  文本行协议 + CRC-4/ITU 校验", size=10, fill="#4a90c4")

    # ── K230 #1 (WiFi 客户端，左) ──
    kw = 210
    k1x = 40
    ky = 115
    kh = 65
    rect(svg, k1x, ky, kw, kh, rx=5, fill="#eaf2f8", stroke="#2471a3", stroke_width="1.8")
    title_bar(svg, k1x, ky, kw, 24, "K230 #1  书标扫码  RT-Smart", fill="#2471a3", size=10)
    text(svg, k1x+kw/2, ky+38, "端侧 QR 解码  quirc vote 3/3", size=9, fill="#444")
    text(svg, k1x+kw/2, ky+52, "1920×1080 → BOOK:ID:CRC4", size=9, fill="#444")

    # ── K230 #2 (WiFi 客户端，右) ──
    k2x = 750
    rect(svg, k2x, ky, kw, kh, rx=5, fill="#eaf2f8", stroke="#2471a3", stroke_width="1.8")
    title_bar(svg, k2x, ky, kw, 24, "K230 #2  视觉导航  RT-Smart", fill="#2471a3", size=10)
    text(svg, k2x+kw/2, ky+38, "★ AprilTag 25h9 PnP 位姿估计", size=9, fill="#c0392b")
    text(svg, k2x+kw/2, ky+52, "UART2 ↔ STM32 桥接", size=9, fill="#444")

    # K230 ←WiFi→ RK3566 连线
    line(svg, k1x+kw, ky+kh/2, 290, ky+kh/2, stroke="#4a90c4", stroke_width="1", dash="4,3")
    arrow_line(svg, 290, ky+kh/2, 310, ky+kh/2, stroke="#4a90c4")
    text(svg, 275, ky+kh/2 - 6, "TCP :9998", size=8, fill="#4a90c4")

    arrow_line(svg, 690, ky+kh/2, 710, ky+kh/2, stroke="#4a90c4")
    line(svg, 710, ky+kh/2, k2x, ky+kh/2, stroke="#4a90c4", stroke_width="1", dash="4,3")
    text(svg, 680, ky+kh/2 - 6, "TCP :9999", size=8, fill="#4a90c4")

    # ── RK3566 中心节点 ──
    rx, rw = 280, 440
    ry = 195
    rh = 62
    rect(svg, rx, ry, rw, rh, rx=6, fill="#e3edf8", stroke="#1a5276", stroke_width="2.5")
    title_bar(svg, rx, ry, rw, 26, "RK3566  边缘调度节点  Cortex-A55×4  Linux 6.1", fill="#1a5276", size=12)
    text(svg, rx+rw/2, ry+40, "C++ HTTP 服务 + SQLite  ·  K230 Bridge（TCP+CRC4）  ·  SenseVoice ASR + ERes2Net 声纹", size=10, fill="#2c3e50")
    text(svg, rx+rw/2, ry+54, "★ DeepSeek (云端) ⇄ Qwen3-0.6B (本地) 双引擎  ·  任务优先队列  ·  HDMI + 小程序接入", size=10, fill="#2c3e50")

    # ── 外设 (RK3566 下方) ──
    pw = 200
    pg = 14

    # OV5695
    ox = 40
    py = 280
    ph = 72
    rect(svg, ox, py, pw, ph, rx=5, fill="#eef7e3", stroke="#27ae60", stroke_width="1.8")
    title_bar(svg, ox, py, pw, 22, "OV5695  任务扫码", fill="#27ae60", size=10)
    text(svg, ox+pw/2, py+36, "MIPI CSI 直连 RK3566", size=9, fill="#444")
    text(svg, ox+pw/2, py+52, "640×480  QRCodeDetector", size=9, fill="#444")

    # EM20L
    ex = ox + pw + pg
    rect(svg, ex, py, pw, ph, rx=5, fill="#eef7e3", stroke="#27ae60", stroke_width="1.8")
    title_bar(svg, ex, py, pw, 22, "EM20L  标签打印", fill="#27ae60", size=10)
    text(svg, ex+pw/2, py+36, "UART9 直连 RK3566", size=9, fill="#444")
    text(svg, ex+pw/2, py+52, "热敏 203DPI  50×50mm", size=9, fill="#444")

    # STM32F407
    sx = ex + pw + pg
    sw = 300
    rect(svg, sx, py, sw, ph, rx=5, fill="#e3f5e8", stroke="#1e8449", stroke_width="2")
    title_bar(svg, sx, py, sw, 22, "STM32F407  主控  RT-Thread 标准版", fill="#1e8449", size=10)
    text(svg, sx+sw/2, py+36, "UART4/5 直连 RK3566  ·  Zigbee (CC2530) 广播协调  ·  RS485 总线冗余", size=9, fill="#444")
    text(svg, sx+sw/2, py+52, "★ 信号量 · 互斥量 · 事件集  多线程实时调度  ·  7 路步进电机 PID 闭环", size=9, fill="#c0392b")

    # 连线：外设 → RK3566
    arrow_line(svg, ox+pw/2, py, rx+80, ry+rh, stroke="#27ae60", stroke_width="1.3")
    text(svg, ox+pw/2, py-6, "MIPI", size=8, fill="#27ae60")
    arrow_line(svg, ex+pw/2, py, rx+200, ry+rh, stroke="#27ae60", stroke_width="1.3")
    text(svg, ex+pw/2, py-6, "UART9", size=8, fill="#27ae60")
    arrow_line(svg, sx+sw/2, py, rx+rw-80, ry+rh, stroke="#1e8449", stroke_width="1.3")
    text(svg, sx+sw/2, py-6, "UART4/5", size=8, fill="#1e8449")

    # K230 #2 → STM32 (UART2 桥接)
    line(svg, k2x+kw/2, ky+kh, k2x+kw/2, py, stroke="#2471a3", stroke_width="1.2", dash="4,3")
    arrow_line(svg, k2x+kw/2, py, sx+10, py+ph/2, stroke="#2471a3", stroke_width="1.5")
    text(svg, k2x+kw/2 + 15, py - 5, "UART2", size=8, fill="#2471a3")

    # ── 子控制器 (Zigbee 下挂) ──
    zx = sx + sw/2
    zy = 378
    zh = 58

    text(svg, zx, zy+10, "▼ Zigbee (CC2530) 广播 ▼", size=9, fill="#1e8449")

    subs = [
        ("推书装置  STM32F103", "×2 步进电机  + 限位·红外"),
        ("机械爪  STM32F103", "×1 步进电机  + 限位开关"),
        ("三轴运动  STM32F103", "×4 步进电机  PID 闭环"),
    ]
    sub_w = 190
    sub_gap = 10
    total = len(subs) * sub_w + (len(subs)-1) * sub_gap
    sub_x0 = zx - total/2

    for i, (ti, desc) in enumerate(subs):
        bx = sub_x0 + i * (sub_w + sub_gap)
        rect(svg, bx, zy+16, sub_w, zh, rx=4, fill="#e8f5e9", stroke="#27ae60", stroke_width="1.5")
        title_bar(svg, bx, zy+16, sub_w, 20, ti, fill="#27ae60", size=9)
        text(svg, bx+sub_w/2, zy+48, desc, size=8, fill="#555")
        line(svg, zx, zy+25, bx+sub_w/2, zy+16, stroke="#1e8449", stroke_width="1")

    # ── 用户终端 ──
    uy = 468
    uh = 32
    rect(svg, 240, uy, 230, uh, rx=6, fill="#e8f0fe", stroke="#4285f4")
    text(svg, 355, uy+21, "微信小程序  HTTPS Funnel", size=11, bold=True, fill="#1a56b0")
    rect(svg, 530, uy, 230, uh, rx=6, fill="#e8f0fe", stroke="#4285f4")
    text(svg, 645, uy+21, "HDMI 触屏  Kiosk 模式", size=11, bold=True, fill="#1a56b0")

    # 用户终端接入 (通过 WiFi 网络访问 RK3566)
    arrow_line(svg, 390, ry+rh, 355, uy, stroke="#4285f4", stroke_width="1.2")
    arrow_line(svg, 610, ry+rh, 645, uy, stroke="#4285f4", stroke_width="1.2")

    return svg


# ═══════════════════════════════════════════════════════════════
# 图2: 五层软件架构 (三芯异构)
# ═══════════════════════════════════════════════════════════════

def draw_software_stack():
    W, H = 1000, 470
    svg = ET.Element("svg", xmlns=SVG_NS, viewBox=f"0 0 {W} {H}",
                     width=str(W), height=str(H))
    svg.append(svg_elem("rect", width="100%", height="100%", fill="#fff"))
    text(svg, 500, 24, "图2-2  系统软件分层架构", size=14, bold=True)

    label_w = 72
    layer_y = 42
    layer_h = 62
    layer_gap = 4
    inner_h = layer_h - 14  # inner item height

    # ═══════════ 5 层定义: (层名, 颜色, [(标题, 描述), ...]) ═══════════
    layers = [
        ("表示层", "#5b9bd5", [
            ("微信小程序", "3 Tab · QR 任务交互 · HTTPS Funnel"),
            ("HDMI 触屏 + 语音交互", "Kiosk 模式 · 虚拟键盘 · 声纹鉴权 · ASR"),
        ]),
        ("业务逻辑层", "#ed7d31", [
            ("C++ HTTP 服务 + SQLite", "50 条路由 · 5 表 WAL · Bearer 鉴权"),
            ("AI 推理 + 任务队列", "★ DeepSeek ⇄ Qwen3-0.6B 双引擎 · 优先队列 BORROW>RETRIEVE>SHELVE"),
        ]),
        ("中间件服务层", "#8e44ad", [
            ("K230 Bridge v2", "TCP :9998/:9999 · 文本行协议 · CRC4 校验 · 7 状态流转"),
            ("QR 扫码 + 声纹/ASR", "OV5695 V4L2 · ERes2Net · SenseVoice · sherpa-onnx"),
        ]),
        # 下面两层按芯片分 3 列
        ("操作系统层", "#2471a3", [
            ("Linux 6.1 (RK3566)", "V4L2 · ALSA · UART\nDRM/KMS · 88x2bu WiFi"),
            ("RT-Smart (K230 ×2)", "摄像头驱动 · quirc QR\nAprilTag · TCP/IP 协议栈"),
            ("RT-Thread 标准版 (STM32 ×4)", "UART · EXTI · ADC · PWM\n信号量 · 互斥量 · 事件集"),
        ]),
        ("硬件驱动层", "#1e8449", [
            ("RK3566 外设", "MIPI CSI (OV5695)\nUART4/5/9 · HDMI · I2S"),
            ("K230 外设", "GC2093 MIPI · GPIO\nUART2 (STM32 桥接)"),
            ("STM32 外设", "7 路步进电机 · 限位开关\nCC2530 Zigbee · RS485"),
        ]),
    ]

    for layer_idx, (name, color, items) in enumerate(layers):
        y = layer_y + layer_idx * (layer_h + layer_gap)
        n_items = len(items)  # 2 or 3

        # Layer outer box
        rect(svg, 30, y, 940, layer_h, rx=5, fill="#fafafa", stroke=color, stroke_width="1.8")

        # Left label
        rect(svg, 30, y, label_w, layer_h, rx=5, fill=color, stroke="none")
        svg.append(svg_elem("rect", x=30+label_w-6, y=y, width=12, height=layer_h,
                            fill=color, stroke="none"))
        text(svg, 30+label_w/2, y + layer_h/2 + 5, name, size=12, bold=True, fill="#fff")

        # Item boxes
        item_x_start = 30 + label_w + 12
        total_w = 940 - label_w - 16
        item_gap = 10
        item_w = (total_w - (n_items - 1) * item_gap) / n_items

        for i, (title, desc) in enumerate(items):
            ix = item_x_start + i * (item_w + item_gap)
            iy = y + 7

            rect(svg, ix, iy, item_w, inner_h, rx=4, fill="#fff", stroke="#ddd", stroke_width="1")
            text(svg, ix + 12, iy + 18, title, size=10, bold=True, fill="#333", anchor="start")

            # desc may contain multiple lines
            lines = desc.split("\n")
            for li, ltext in enumerate(lines):
                is_star = ltext.startswith("★")
                dc = "#c0392b" if is_star else "#555"
                text(svg, ix + 12, iy + 34 + li * 13, ltext, size=8, fill=dc, anchor="start")

    # ═══════════ 右侧芯片列标注 ═══════════
    # 列出 OS/Driver 层每列的 x 中心
    # 3 columns when n_items=3
    os_items = layers[3][2]  # 3 items
    n_cols = len(os_items)
    col_centers = []
    item_x_start = 30 + label_w + 12
    total_w = 940 - label_w - 16
    item_w3 = (total_w - (n_cols - 1) * item_gap) / n_cols
    for i in range(n_cols):
        cx = item_x_start + i * (item_w3 + item_gap) + item_w3 / 2
        col_centers.append(cx)

    # 芯片标注 (在 OS 层下方)
    chip_y = layer_y + 4 * (layer_h + layer_gap) + layer_h + 14
    chip_labels = ["RK3566  Cortex-A55×4", "K230 ×2  RISC-V 双核", "STM32 ×4  Cortex-M3/M4"]
    for i, (cx, cl) in enumerate(zip(col_centers, chip_labels)):
        text(svg, cx, chip_y, cl, size=9, fill="#444")
        # 竖线连接 OS 和 Driver 层
        line(svg, cx, chip_y - layer_h + 14, cx, chip_y - 8, stroke="#aaa", stroke_width="1")

    # ── 底部标注 ──
    text(svg, 500, chip_y + 28, "★ 三芯异构 · RT-Thread 全栈覆盖:  Linux (应用+AI)  +  RT-Smart (视觉感知)  +  RT-Thread 标准版 (实时控制)",
         size=10, fill="#c0392b")

    return svg


# ═══════════════════════════════════════════════════════════════
# 图3: 云端-本地双引擎 AI 管线
# ═══════════════════════════════════════════════════════════════

def draw_ai_pipeline():
    svg = ET.Element("svg", xmlns=SVG_NS, viewBox="0 0 1000 310",
                     width="1000", height="310")
    svg.append(svg_elem("rect", width="100%", height="100%", fill="#fff"))
    text(svg, 500, 28, "图2-3  AI 语音处理 — 云端-本地双引擎管线", size=14, bold=True)

    stages = [
        ("音频采集", "RK809 Codec\n16kHz S32_LE\nADC DC偏置校准"),
        ("声纹验证", "ERes2Net 512维嵌入\n余弦相似度 ≥0.5\n< 50ms 响应"),
        ("端侧 ASR", "SenseVoice-Small\nCPU推理 RTF≈0.6\n离线可用"),
        ("书名提取 + 分类", "★ 双引擎 failover\n有网: DeepSeek v4-pro\n断网: Qwen3-0.6B\nLoRA微调 80%准确率"),
        ("书库纠错", "拼音模糊匹配\n编辑距离阈值过滤\n★ 414万本地书名索引"),
        ("结构化输出", "书名 + 四分类\n自动打印QR标签\n入库 SQLite"),
    ]

    fills = ["#eaf2f8", "#e8f0fe", "#fef9e7", "#fdedec", "#eef7e3", "#f0ecf8"]
    strokes = ["#1a5276", "#4285f4", "#b7950b", "#c0392b", "#27ae60", "#8470c9"]
    x_start = 40
    stage_w = 142
    gap = 10

    for i, (title, desc) in enumerate(stages):
        x = x_start + i * (stage_w + gap)
        y = 50
        lines = desc.split("\n")
        h = 180

        rect(svg, x, y, stage_w, h, rx=6, fill=fills[i], stroke=strokes[i], stroke_width="2")

        # Title bar
        title_bar(svg, x, y, stage_w, 28, title, fill=strokes[i], size=11)

        # Content lines
        for li, line in enumerate(lines):
            is_label = line.startswith("★")
            c = "#c0392b" if is_label else "#444"
            sz = 9 if li > 0 else 10
            bd = is_label
            text(svg, x + stage_w/2, y + 48 + li * 18, line,
                 size=sz, fill=c, bold=bd)

        # Arrow between stages
        if i < len(stages) - 1:
            arrow_line(svg, x + stage_w + 1, y + h/2, x + stage_w + gap - 2, y + h/2,
                       stroke=strokes[i], stroke_width="1.5")

    # Bottom annotation
    text(svg, 500, 260, "云端优先 · 断网 < 300ms 自动切换本地 · 离线可用 vs 云端质量无缝平滑降级",
         size=11, bold=True, fill="#c0392b")

    # Dual engine detail
    dashed_rect(svg, 510, 278, 470, 26, fill="#fef9e7")
    text(svg, 745, 295, "本地模型: Qwen3-0.6B  LoRA微调  Q4_K_M量化 (379MB)  llama.cpp推理  8.25 tok/s",
         size=9, fill="#8b7300")

    return svg


# ═══════════════════════════════════════════════════════════════
if __name__ == "__main__":
    diagrams = {
        "edge_computing_topology.svg": draw_edge_computing_topology,
        "software_stack.svg": draw_software_stack,
        "ai_pipeline.svg": draw_ai_pipeline,
    }
    for fname, fn in diagrams.items():
        svg = fn()
        tree = ET.ElementTree(svg)
        ET.indent(tree, space="  ")
        path = os.path.join(OUT_DIR, fname)
        tree.write(path, encoding="utf-8", xml_declaration=True)
        print(f"✅ {path}")
