#!/usr/bin/env python3
"""模拟麦克风录音 (RK809 codec, card 1, hw:1,0)

依赖: arecord + sox (板载已有)
用法:
    from analog_mic import capture_16k
    audio = capture_16k(5)  # 录5秒, 返回 np.ndarray float32 [-1,1] @ 16kHz mono
"""

import subprocess
import tempfile
import os
import numpy as np
import soundfile as sf

CARD = 1
DEVICE = "hw:1,0"


def _setup_mixer():
    """配置 RK809 模拟麦克风路径 — OFF→Main Mic 上电序列消除 ADC DC 偏置"""
    import time

    subprocess.run(["amixer", "-c", str(CARD), "sset", "ADC", "255,255"],
                   capture_output=True, timeout=3)
    subprocess.run(["amixer", "-c", str(CARD), "sset", "ADC PGA Gain", "15,15"],
                   capture_output=True, timeout=3)
    subprocess.run(["amixer", "-c", str(CARD), "sset", "MIC Boost Gain", "3,3"],
                   capture_output=True, timeout=3)

    # MIC OFF → Main Mic 上电序列（关键：触发 ADC 路径校准消除 DC 偏置）
    subprocess.run(["amixer", "-c", str(CARD), "set", "Capture MIC Path", "MIC OFF"],
                   capture_output=True, timeout=3)
    time.sleep(0.15)
    subprocess.run(["amixer", "-c", str(CARD), "set", "Capture MIC Path", "Main Mic"],
                   capture_output=True, timeout=3)
    time.sleep(0.2)


def capture_16k(duration: float) -> np.ndarray:
    """录制指定时长，返回 float32 numpy array @ 16kHz mono

    I2S 配置 VDW=24, 32-bit slot. 需要 S32_LE 录音后提取高 24 位.
    """
    _setup_mixer()
    stereo_path = None
    try:
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            stereo_path = tmp.name

        subprocess.run(
            ["arecord", "-D", DEVICE, "-f", "S32_LE", "-r", "16000",
             "-c", "2", "-d", str(int(duration)), stereo_path],
            check=True, capture_output=True, timeout=int(duration) + 5,
        )

        data, _sr = sf.read(stereo_path)
        if len(data.shape) > 1:
            data = data[:, 0]  # 左声道

        # I2S VDW=24 in 32-bit slot: 音频在 bits[31:8], bits[7:0] 为填充
        # S32_LE 读回 32-bit 值, 右移 8 位得 24-bit 样本
        i32 = (data * 2147483647.0).astype(np.int64)
        audio_24 = i32 >> 8
        audio = np.clip(audio_24.astype(np.float64) / 8388607.0, -1.0, 1.0)
        return audio.astype(np.float32)
    finally:
        if stereo_path and os.path.exists(stereo_path):
            os.unlink(stereo_path)


def check_analog_mic() -> bool:
    """检查模拟麦克风是否可用"""
    try:
        result = subprocess.run(
            ["arecord", "-D", DEVICE, "-f", "S32_LE", "-r", "16000", "-c", "2", "-d", "1",
             "/dev/null"],
            capture_output=True, timeout=5,
        )
        return result.returncode == 0
    except Exception:
        return False


if __name__ == "__main__":
    ok = check_analog_mic()
    print(f"Analog mic (RK809 hw:1,0): {'OK' if ok else 'NOT AVAILABLE'}")
    if ok:
        print("Recording 3s test...")
        audio = capture_16k(3)
        print(f"  samples={len(audio)} min={audio.min():.4f} max={audio.max():.4f} "
              f"rms={np.sqrt((audio**2).mean()):.4f}")
