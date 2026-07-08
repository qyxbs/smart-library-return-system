#!/usr/bin/env python3
"""ASR — SenseVoice via sherpa-onnx (CPU, NPU reserved for future)"""
import os
import numpy as np
import sherpa_onnx

MODEL_DIR = "/userdata/voice_app/models"

def _create_recognizer():
    rknn = os.path.join(MODEL_DIR, "sensevoice.rknn")
    onnx = os.path.join(MODEL_DIR, "model.int8.onnx")
    tokens = os.path.join(MODEL_DIR, "tokens.txt")

    if os.path.exists(rknn):
        print("[ASR] Loading SenseVoice RKNN (NPU mode)...", flush=True)
        try:
            return sherpa_onnx.OfflineRecognizer.from_sense_voice(
                model=rknn, tokens=tokens, use_itn=True, language="zh")
        except Exception as e:
            print(f"[ASR] NPU failed: {e}, falling back to CPU", flush=True)

    print("[ASR] Loading SenseVoice ONNX (CPU mode)...", flush=True)
    return sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=onnx, tokens=tokens, use_itn=True, language="zh")

class ASREngine:
    def __init__(self):
        self.rec = _create_recognizer()

    def transcribe(self, audio: np.ndarray, sample_rate: int = 16000) -> str:
        # Ensure float32 [-1, +1]
        if audio.dtype == np.int16:
            audio = audio.astype(np.float32) / 32768.0
        elif audio.dtype != np.float32:
            audio = audio.astype(np.float32)
        if sample_rate != 16000:
            import scipy.signal
            audio = scipy.signal.resample_poly(audio, 16000, sample_rate)

        stream = self.rec.create_stream()
        stream.accept_waveform(16000, audio.tolist())
        self.rec.decode_stream(stream)
        return stream.result.text.strip()

_asr = None
def get_asr():
    global _asr
    if _asr is None:
        _asr = ASREngine()
    return _asr
