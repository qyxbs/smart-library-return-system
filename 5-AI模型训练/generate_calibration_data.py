import numpy as np
import os
import librosa
import soundfile as sf

def generate_calibration_audio():
    """生成用于模型校准的音频数据"""
    # 创建校准数据目录
    os.makedirs("calibration_data", exist_ok=True)

    # 生成几种不同类型的校准音频
    sample_rates = [16000, 44100]

    for i in range(5):  # 生成5个校准样本
        # 生成不同长度和采样率的音频
        sr = sample_rates[i % len(sample_rates)]
        duration = 2  # 2秒
        length = sr * duration

        # 生成白噪声 + 正弦波混合信号，模拟真实语音
        noise = np.random.normal(0, 0.1, length)
        freq = 440 + (i * 100)  # 基频440Hz，逐个增加
        sine_wave = 0.3 * np.sin(2 * np.pi * freq * np.arange(length) / sr)
        audio_data = noise + sine_wave

        # 标准化音频
        audio_data = audio_data / np.max(np.abs(audio_data)) * 0.8

        # 保存音频文件
        filename = f"calibration_data/calib_{i:03d}_{sr}hz.wav"
        sf.write(filename, audio_data, sr)
        print(f"生成校准音频: {filename}, shape: {audio_data.shape}, sr: {sr}")

        # 也生成对应的numpy数组用于直接输入
        np.save(f"calibration_data/calib_{i:03d}_{sr}hz.npy", audio_data.astype(np.float32))

if __name__ == "__main__":
    generate_calibration_audio()
    print("校准数据生成完成")
