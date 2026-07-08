#!/userdata/python/bin/python3
"""
EM20L 热敏标签打印机 — 50mm×50mm 标签 @ 203DPI
==============================================
用法: python3 print_label.py <book_id> <title>
       LABEL_DEBUG=1 python3 print_label.py <book_id> <title>  # 调试模式

参考: EM20L 使用说明书 (标签指令集 §7.2-§7.9)
      ESP32-S3 已验证参考代码 (EM20L_QRCode_Print/main/app_main.c)

标签物理规格:
  - 尺寸: 50mm × 50mm (400 × 400 dots @ 203DPI, 1 dot = 0.125mm)
  - 间距: 2mm (≈16 dots, 由打印机间隙传感器检测)
  - 打印头宽度: 58mm (464 dots), 标签 50mm 在范围内
"""

import sys
import os
import struct

# ============================================================
# CRC-4/ITU (与 k230_bridge_v2.py compute_crc4() 完全一致)
# 仅处理每个字节低 4 位, K230 #1 联调已验证
# ============================================================

def crc4(data: bytes) -> int:
    """CRC-4/ITU: poly=0x3, init=0x0, 每字节仅取低 4 位"""
    crc = 0x0
    for byte in data:
        crc ^= (byte & 0xF)
        for _ in range(4):
            crc = ((crc << 1) ^ 0x3) & 0xF if crc & 0x8 else (crc << 1) & 0xF
    return crc & 0xF


def make_qr_text(book_id: str) -> str:
    """QR 码内容: BOOK:<id>:<CRC4> (CRC4 大写 4 位 hex)"""
    prefix = f"BOOK:{book_id}"
    return f"{prefix}:{crc4(prefix.encode('ascii')):04X}"


# ============================================================
# 50mm × 50mm 标签布局 (203 DPI: 1 dot = 0.125mm)
# ============================================================

LABEL_W = 400   # 50mm
LABEL_H = 400   # 50mm

# QR 码 — 右下角, 版本自动, 纠错等级 Q(25%), 模块 8 dots (max)
QR_ECC     = 3    # Q 级纠错 ≈25%
QR_MODULE  = 8    # 每模块 8 dots (上限, 超过不打印)
QR_SIZE    = 200  # 25×8 (version 2, 自动选版)
QR_X       = LABEL_W - QR_SIZE - 20   # 180 (右边距 20)
QR_Y       = LABEL_H - QR_SIZE - 20   # 180 (下边距 20)

# 书名 — 上方居中, 1x 字体
TITLE_X    = 200  # 居中参考点
TITLE_Y    = 40
TITLE_SIZE = 0x11  # 1x 字体 (约12dots)


# ============================================================
# EM20L 指令构建 (按官方手册 §7.2-§7.9)
# ============================================================

def u16le(v: int) -> bytes:
    return struct.pack("<H", v)


def build_label(book_id: str, title: str) -> bytes:
    """构建完整 EM20L 标签指令序列"""
    cmd = bytearray()

    # 0. 初始化打印机 (ESC @)
    cmd.extend(b"\x1B\x40")

    # 1. 标签开始 §7.2
    #    1A 5B 01 [X:2 LE] [Y:2 LE] [W:2 LE] [H:2 LE] [Rotate:1]
    cmd.extend(b"\x1A\x5B\x01")        # 标签模式
    cmd.extend(u16le(0))                # X 偏移 = 0
    cmd.extend(u16le(0))                # Y 偏移 = 0
    cmd.extend(u16le(LABEL_W))          # 宽度 400
    cmd.extend(u16le(LABEL_H))          # 高度 400
    cmd.append(0x00)                    # 不旋转

    # 2. QR 二维码 §7.9
    #    1A 31 00 [ver:1] [ECC:1] [X:2] [Y:2] [UnitW:1] [Rotate:1] [data] 00
    qr_text = make_qr_text(book_id)
    cmd.extend(b"\x1A\x31\x00")        # QRCode 指令
    cmd.append(0x00)                    # version = 0 (自动选版)
    cmd.append(QR_ECC)                  # ECC = 3 (Q级)
    cmd.extend(u16le(QR_X))             # X 坐标
    cmd.extend(u16le(QR_Y))             # Y 坐标
    cmd.append(QR_MODULE)               # 模块大小
    cmd.append(0x00)                    # 不旋转
    cmd.extend(qr_text.encode("ascii"))
    cmd.append(0x00)                    # 字符串终止

    # 3. 设置居中 §8.5
    cmd.extend(b"\x1B\x61\x01")        # ESC a 1 = 居中

    # 4. 书名文字 (字体模式) §7.7
    #    1A 54 01 [X:2] [Y:2] 18 00 [Style:1] [Size:1] [GBK] 00
    title_gbk = title.encode("gbk", errors="replace")
    cmd.extend(b"\x1A\x54\x01")        # 文本指令 (字体模式)
    cmd.extend(u16le(TITLE_X))
    cmd.extend(u16le(TITLE_Y))
    cmd.extend(b"\x18\x00")            # 固定头
    cmd.append(0x00)                   # Style = 正常
    cmd.append(TITLE_SIZE)             # 字号
    cmd.extend(title_gbk)
    cmd.append(0x00)

    # 5. 标签结束 §7.3 + 打印 §7.4
    cmd.extend(b"\x1A\x5D\x00")        # 标签结束
    cmd.extend(b"\x1A\x4F\x00")        # 打印 1 张

    # 6. 走纸偏移清零 (防多吐空白标签)
    cmd.extend(b"\x1B\x64\x00")        # ESC d 0

    return bytes(cmd)


# ============================================================
# 工具函数
# ============================================================

def dump_hex(data: bytes):
    """十六进制 dump"""
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_s = " ".join(f"{b:02X}" for b in chunk)
        asc_s = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
        print(f"  {i:04X}: {hex_s:<48s} {asc_s}")


# ============================================================
# 主入口
# ============================================================

def main():
    if len(sys.argv) < 3:
        print(f"用法: {sys.argv[0]} <book_id> <title>", file=sys.stderr)
        print(f"示例: {sys.argv[0]} 5891198275 '三体'", file=sys.stderr)
        print(f"调试: LABEL_DEBUG=1 {sys.argv[0]} 5891198275 '三体'", file=sys.stderr)
        sys.exit(1)

    book_id = sys.argv[1]
    title = sys.argv[2]
    qr_text = make_qr_text(book_id)
    cmd = build_label(book_id, title)

    print(f"[Label] 50×50mm | {LABEL_W}×{LABEL_H} dots @ 203DPI")
    print(f"[QR]    {qr_text}")
    print(f"[QR]    auto-ver, ECC=Q, W={QR_MODULE} -> ~{QR_SIZE}x{QR_SIZE} dots, pos=({QR_X},{QR_Y})")
    print(f"[Title] {title} | GBK={len(title.encode('gbk'))}B | pos=({TITLE_X},{TITLE_Y}) size=0x{TITLE_SIZE:02X}")
    print(f"[Cmd]   {len(cmd)} bytes -> /dev/ttyS9")

    if os.environ.get("LABEL_DEBUG"):
        print("\n[DEBUG] 指令序列 hex dump:")
        dump_hex(cmd)

        # 验证关键字节
        ok = "✅"; no = "❌"
        init_ok = ok if cmd[:2] == b'\x1b\x40' else no
        start_ok = ok if cmd[2:5] == b'\x1a\x5b\x01' else no
        qr_ok = ok if b'\x1a\x31\x00' in cmd else no
        text_cnt = cmd.count(b'\x1a\x54\x01')
        end_ok = ok if b'\x1a\x5d\x00' in cmd else no
        print_ok = ok if b'\x1a\x4f\x00' in cmd else no
        feed_ok = ok if cmd[-3:] == b'\x1b\x64\x00' else no
        print("\n[DEBUG] 结构验证:")
        print(f"  Init:    1B 40 -> {init_ok}")
        print(f"  Start:   1A 5B 01 -> {start_ok}")
        print(f"  QR cmd:  1A 31 00 -> {qr_ok}")
        print(f"  Text:    1A 54 01 -> {text_cnt} occurrences")
        print(f"  End:     1A 5D 00 -> {end_ok}")
        print(f"  Print:   1A 4F 00 -> {print_ok}")
        print(f"  Feed:    1B 64 00 -> {feed_ok}")
        return

    # 写入串口
    try:
        with open("/dev/ttyS9", "wb") as f:
            f.write(cmd)
        print("[OK] 标签打印指令已发送")
    except Exception as e:
        print(f"[ERROR] 无法写入 /dev/ttyS9: {e}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
