#!/usr/bin/env python3
"""
智能书架 - 语音AI调试平台
声纹注册 + 语音书名识别（仅识别管理员）
"""
import os
# ONNX Runtime: 禁用 arena allocator，节省 ~384MB RAM（必须在 import sherpa_onnx 之前设置）
os.environ.setdefault("ORT_DISABLE_ARENA", "1")
# Serper API Key: 联网兜底
# SERPER_API_KEY: 从环境变量读取，用于联网搜索兜底
# os.environ.setdefault("SERPER_API_KEY", "your-serper-api-key")

import sys, json, time, base64, io, hashlib, subprocess, datetime, threading, gc
from pathlib import Path
import numpy as np
import soundfile as sf
from flask import Flask, render_template_string, request, jsonify
from flask_socketio import SocketIO, emit
from flask_cors import CORS

# PDM MEMS mic reader (RT-Thread shared memory via C helper — NO Python mmap)
try:
    from pdm_reader import capture_16k as pdm_capture_16k, resample_8k_to_16k, check_pdm_running
    PDM_AVAILABLE = check_pdm_running()
    print(f"[PDM] RT-Thread PDM mic: {'AVAILABLE' if PDM_AVAILABLE else 'NOT RUNNING'}", flush=True)
except Exception as e:
    PDM_AVAILABLE = False
    pdm_capture_16k = None
    resample_8k_to_16k = None
    print(f"[PDM] Not available: {e}", flush=True)

# Analog mic (RK809 codec, card 1, hw:1,0 — 咪头插 AUDIO 口)
try:
    from analog_mic import capture_16k as analog_capture_16k, check_analog_mic
    ANALOG_MIC_AVAILABLE = check_analog_mic()
    print(f"[Analog] RK809 analog mic: {'AVAILABLE' if ANALOG_MIC_AVAILABLE else 'NOT AVAILABLE'}", flush=True)
except Exception as e:
    ANALOG_MIC_AVAILABLE = False
    analog_capture_16k = None
    print(f"[Analog] Not available: {e}", flush=True)

# ==================== 配置 ====================
BASE_DIR = "/userdata/voice_app"
MODEL_DIR = os.path.join(BASE_DIR, "models")
VOICEPRINT_DIR = os.path.join(BASE_DIR, "voiceprints")
os.makedirs(VOICEPRINT_DIR, exist_ok=True)
os.makedirs(MODEL_DIR, exist_ok=True)

DASHSCOPE_KEY = os.environ.get("DASHSCOPE_KEY", "")
DEEPSEEK_KEY = os.environ.get("DEEPSEEK_KEY", "")
OSS_KEY_ID = os.environ.get("OSS_KEY_ID", "")
OSS_KEY_SECRET = os.environ.get("OSS_KEY_SECRET", "")
OSS_BUCKET = os.environ.get("OSS_BUCKET", "")
OSS_ENDPOINT = os.environ.get("OSS_ENDPOINT", "")

CATEGORIES = ["教辅", "文学", "科技", "其他"]

# SSL — phone browser requires HTTPS for getUserMedia
CERT_FILE = os.path.join(BASE_DIR, "voice_cert.pem")
KEY_FILE = os.path.join(BASE_DIR, "voice_key.pem")

# ==================== 声纹模型初始化 ====================
import sherpa_onnx

def init_voiceprint():
    """初始化声纹提取器"""
    emb_model = os.path.join(MODEL_DIR, "3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx")
    if not os.path.exists(emb_model):
        return None
    cfg = sherpa_onnx.SpeakerEmbeddingExtractorConfig(model=emb_model)
    extractor = sherpa_onnx.SpeakerEmbeddingExtractor(cfg)
    manager = sherpa_onnx.SpeakerEmbeddingManager(extractor.dim)
    # Load existing voiceprints
    for f in Path(VOICEPRINT_DIR).glob("*.npy"):
        name = f.stem
        emb = np.load(str(f))
        manager.add(name, emb)
    return {"extractor": extractor, "manager": manager}

vp = init_voiceprint()

# ==================== Flask App ====================
app = Flask(__name__)
CORS(app)
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

# ==================== HTML 模板 ====================
INDEX_HTML = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>智能书架 - 语音AI调试</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);min-height:100vh;color:#eee}
.header{background:rgba(0,0,0,.3);padding:20px 40px;display:flex;justify-content:space-between;align-items:center}
.header h1{font-size:1.5em}
.cards{display:flex;gap:30px;padding:40px;max-width:1200px;margin:0 auto;flex-wrap:wrap}
.card{flex:1;min-width:300px;background:rgba(255,255,255,.08);border-radius:16px;padding:30px;text-align:center;border:1px solid rgba(255,255,255,.1);transition:.3s;text-decoration:none;color:inherit;display:block}
.card:hover{transform:translateY(-4px);background:rgba(255,255,255,.12);box-shadow:0 8px 30px rgba(0,0,0,.3)}
.card h2{font-size:1.8em;margin-bottom:10px}
.card p{color:#aaa;font-size:.95em}
.card .icon{font-size:3em;margin-bottom:15px}
.status{display:inline-flex;align-items:center;gap:8px}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block}
.dot.green{background:#4caf50}
.dot.red{background:#f44336}
</style>
</head>
<body>
<div class="header">
  <h1>📚 智能书架 - 语音AI调试平台</h1>
  <div class="status"><span class="dot {{'green' if vp_ok else 'red'}}"></span> 声纹模型: {{'就绪' if vp_ok else '未加载'}}</div>
</div>
<div class="cards">
  <a href="/enroll" class="card">
    <div class="icon">🎤</div><h2>声纹注册</h2><p>采集管理员语音，建立声纹库</p>
  </a>
  <a href="/recognize" class="card">
    <div class="icon">📖</div><h2>语音书名识别</h2><p>说出书名 → 自动分类 → 录入系统</p>
  </a>
</div>
</body>
</html>"""

ENROLL_HTML = '''
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>声纹注册</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);min-height:100vh;color:#eee;display:flex;flex-direction:column;align-items:center}
.header{width:100%;background:rgba(0,0,0,.3);padding:15px 40px;display:flex;align-items:center;gap:20px}
.header a{color:#aaa;text-decoration:none}.header a:hover{color:#fff}
.container{max-width:600px;width:100%;padding:40px 20px}
h2{margin-bottom:20px;text-align:center}
.form-group{margin-bottom:20px}
label{display:block;margin-bottom:8px;color:#aaa}
input[type=text]{width:100%;padding:12px 16px;border-radius:8px;border:1px solid rgba(255,255,255,.2);background:rgba(255,255,255,.08);color:#fff;font-size:1em}
.btn{display:block;width:100%;padding:14px;border:none;border-radius:8px;font-size:1.1em;cursor:pointer;margin:10px 0;transition:.2s}
.btn-record{background:#2196F3;color:#fff}.btn-record:hover{background:#1e88e5}
.btn-record.recording{background:#f44336;animation:pulse 1.5s infinite}
.btn-analog{background:#4caf50;color:#fff}.btn-analog:hover{background:#43a047}
.btn-analog.recording{background:#f44336;animation:pulse 1.5s infinite}
.btn-pdm{background:#ff9800;color:#fff}.btn-pdm:hover{background:#f57c00}
.btn-pdm.recording{background:#f44336;animation:pulse 1.5s infinite}
.btn-print{background:#4caf50;color:#fff;display:none}.btn-print:hover{background:#43a047}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.7}}
.msg{padding:12px;border-radius:8px;margin:10px 0;text-align:center}
.msg.ok{background:rgba(76,175,80,.2);border:1px solid #4caf50}
.msg.err{background:rgba(244,67,54,.2);border:1px solid #f44336}
.msg.info{background:rgba(33,150,243,.2);border:1px solid #2196F3}
.timer{text-align:center;font-size:3em;margin:15px 0;font-variant-numeric:tabular-nums}
.spinner{display:none;width:40px;height:40px;margin:20px auto;border:4px solid rgba(255,255,255,.1);border-top-color:#ff9800;border-radius:50%;animation:spin .8s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.voices{display:flex;flex-wrap:wrap;gap:8px;margin-top:15px}
.chip{background:rgba(255,255,255,.1);padding:6px 14px;border-radius:20px;font-size:.9em}
.divider{display:flex;align-items:center;gap:15px;margin:20px 0;color:#666;font-size:.85em}
.divider::before,.divider::after{content:'';flex:1;height:1px;background:rgba(255,255,255,.1)}
.pdm-status{font-size:.8em;color:#aaa;text-align:center;margin-bottom:5px}
</style>
</head>
<body>
<div class="header"><a href="/">&larr; 首页</a><h3>声纹注册</h3></div>
<div class="container">
  <div class="form-group">
    <label>管理员姓名</label>
    <input type="text" id="name" placeholder="输入姓名...">
  </div>

  <!-- 模拟麦克风注册 (主) -->
  <div class="pdm-status" id="analogStatus">模拟麦克风: 检测中...</div>
  <button class="btn btn-analog" id="analogBtn" onclick="analogEnroll()">板载咪头 注册 (5秒)</button>
  <div class="timer" id="analogTimer" style="display:none">5</div>


  <div class="spinner" id="spinner"></div>
  <div id="msg"></div>
  <div><h4 style="margin:15px 0 10px;color:#aaa">已注册声纹</h4><div class="voices" id="voices">加载中...</div></div>
</div>
<script>
let mediaRecorder, audioChunks=[], recording=false;

// ---- 模拟麦克风注册 (主) ----
let analogEnrolling = false;
async function analogEnroll() {
  if (analogEnrolling) return;
  const name = document.getElementById('name').value.trim();
  if (!name) { showMsg('请输入姓名','err'); return; }
  analogEnrolling = true;
  const btn = document.getElementById('analogBtn');
  const timer = document.getElementById('analogTimer');
  const dur = 5;
  btn.classList.add('recording');
  btn.textContent = '录音中...';
  btn.disabled = true;

  let remain = dur;
  timer.style.display = 'block';
  timer.textContent = remain;
  const ti = setInterval(() => { remain--; timer.textContent = remain; if (remain <= 0) clearInterval(ti); }, 1000);

  try {
    const resp = await fetch('/api/analog-enroll', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({name, duration: dur}),
    });
    clearInterval(ti);
    timer.style.display = 'none';
    btn.classList.remove('recording');
    btn.textContent = '板载咪头 注册 (5秒)';
    btn.disabled = false;
    analogEnrolling = false;

    const d = await resp.json();
    showMsg(d.ok ? '✅ 注册成功: '+d.name+' (共'+d.count+'人)' : '❌ 失败: '+(d.error||''), d.ok?'ok':'err');
    if (d.ok) { document.getElementById('name').value = ''; loadVoices(); }
  } catch (e) {
    clearInterval(ti);
    timer.style.display = 'none';
    btn.classList.remove('recording');
    btn.textContent = '板载咪头 注册 (5秒)';
    btn.disabled = false;
    analogEnrolling = false;
    showMsg('网络错误: '+e.message,'err');
  }
}

// ---- PDM MEMS 麦克风注册 ----
let pdmEnrolling = false;
async function pdmEnroll() {
  if (pdmEnrolling) return;
  const name = document.getElementById('name').value.trim();
  if (!name) { showMsg('请输入姓名','err'); return; }
  pdmEnrolling = true;
  const btn = document.getElementById('pdmBtn');
  const timer = document.getElementById('pdmTimer');
  const dur = 5;
  btn.classList.add('recording');
  btn.textContent = 'PDM录音中...';
  btn.disabled = true;

  let remain = dur;
  timer.style.display = 'block';
  timer.textContent = remain;
  const ti = setInterval(() => { remain--; timer.textContent = remain; if (remain <= 0) clearInterval(ti); }, 1000);

  try {
    const resp = await fetch('/api/pdm-enroll', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({name, duration: dur}),
    });
    clearInterval(ti);
    timer.style.display = 'none';
    btn.classList.remove('recording');
    btn.textContent = 'PDM MEMS 注册 (5秒)';
    btn.disabled = false;
    pdmEnrolling = false;

    const d = await resp.json();
    showMsg(d.ok ? '✅ 注册成功: '+d.name+' (共'+d.count+'人)' : '❌ 失败: '+(d.error||''), d.ok?'ok':'err');
    if (d.ok) { document.getElementById('name').value = ''; loadVoices(); }
  } catch (e) {
    clearInterval(ti);
    timer.style.display = 'none';
    btn.classList.remove('recording');
    btn.textContent = 'PDM MEMS 注册 (5秒)';
    btn.disabled = false;
    pdmEnrolling = false;
    showMsg('网络错误: '+e.message,'err');
  }
}

// ---- 手机麦克风注册 (备选) ----
async function toggleRecord(){
  if(recording){stopRecord();return}
  let name=document.getElementById('name').value.trim();
  if(!name){showMsg('请输入姓名','err');return}
  try{
    let stream=await navigator.mediaDevices.getUserMedia({audio:true});
    let mime='audio/webm;codecs=opus';
    if(!MediaRecorder.isTypeSupported(mime)) mime='audio/webm';
    mediaRecorder=new MediaRecorder(stream,{mimeType:mime});
    audioChunks=[];
    mediaRecorder.ondataavailable=e=>{if(e.data.size>0) audioChunks.push(e.data)};
    mediaRecorder.onstop=()=>processRecording(name);
    mediaRecorder.start();
    recording=true;
    document.getElementById('recordBtn').textContent='录音中...';
    document.getElementById('recordBtn').classList.add('recording');
    document.getElementById('timer').style.display='block';
    let t=3;document.getElementById('timer').textContent=t;
    let timer=setInterval(()=>{t--;document.getElementById('timer').textContent=t;if(t<=0){clearInterval(timer);stopRecord()}},1000);
  }catch(e){showMsg('麦克风错误: '+e.message,'err')}
}
function stopRecord(){
  if(mediaRecorder&&mediaRecorder.state==='recording') mediaRecorder.stop();
  recording=false;
  document.getElementById('recordBtn').textContent='手机麦克风 (3秒)';
  document.getElementById('recordBtn').classList.remove('recording');
  document.getElementById('timer').style.display='none';
}
async function processRecording(name){
  if(audioChunks.length===0){showMsg('未录到音频','err');return}
  showMsg('正在处理音频...','info');
  document.getElementById('spinner').style.display='block';
  try{
    let blob=new Blob(audioChunks);
    let arrayBuf=await blob.arrayBuffer();
    let actx=new AudioContext();
    let audioBuf=await actx.decodeAudioData(arrayBuf);
    await actx.close();
    let pcm;
    if(audioBuf.sampleRate===16000&&audioBuf.numberOfChannels===1){
      pcm=audioBuf.getChannelData(0);
    }else{
      let offline=new OfflineAudioContext(1,Math.ceil(audioBuf.duration*16000),16000);
      let src=offline.createBufferSource();
      src.buffer=audioBuf;
      src.connect(offline.destination);
      src.start();
      let rendered=await offline.startRendering();
      pcm=rendered.getChannelData(0);
    }
    let i16=new Int16Array(pcm.length);
    for(let i=0;i<pcm.length;i++){let s=Math.max(-1,Math.min(1,pcm[i]));i16[i]=s<0?Math.round(s*32768):Math.round(s*32767)}
    let bytes=new Uint8Array(i16.buffer),chunks=[];
    for(let i=0;i<bytes.length;i+=4096)chunks.push(String.fromCharCode(...bytes.slice(i,i+4096)));
    let b64=btoa(chunks.join(''));
    let r=await fetch('/api/enroll',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,b64})});
    let d=await r.json();
    document.getElementById('spinner').style.display='none';
    showMsg(d.ok?'✅ 注册成功: '+d.name:'❌ 失败: '+(d.error||''),d.ok?'ok':'err');
    if(d.ok)loadVoices();
  }catch(e){document.getElementById('spinner').style.display='none';showMsg('处理失败: '+e.message,'err')}
}

function showMsg(text,cls){document.getElementById('msg').innerHTML='<div class="msg '+cls+'">'+text+'</div>'}
async function loadVoices(){
  let r=await fetch('/api/voices'),d=await r.json();
  document.getElementById('voices').innerHTML=d.names.map(n=>'<span class="chip">'+n+'</span>').join('')||'(none)';
}
// Check analog mic status
fetch('/api/analog-status').then(r=>r.json()).then(s => {
  document.getElementById('analogStatus').textContent = s.available
    ? '模拟麦克风: 在线 (RK809)'
    : '模拟麦克风: 离线';
  if (!s.available) {
    document.getElementById('analogBtn').disabled = true;
    document.getElementById('analogBtn').style.opacity = '0.5';
  }
});
// Check PDM status
fetch('/api/pdm-status').then(r=>r.json()).then(s => {
  document.getElementById('pdmStatus').textContent = s.available
    ? 'PDM状态: 在线 | \\u5df2\\u91c7\\u96c6 ' + (s.total_samples/1000).toFixed(0) + 'k \\u6837\\u672c'
    : 'PDM状态: \\u79bb\\u7ebf (RT-Thread\\u672a\\u8fd0\\u884c)';
  if (!s.available) {
    document.getElementById('pdmBtn').disabled = true;
    document.getElementById('pdmBtn').style.opacity = '0.5';
  }
});
loadVoices();
</script>
</body>
</html>'''

RECOGNIZE_HTML = '''
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>语音书名识别</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);min-height:100vh;color:#eee;display:flex;flex-direction:column;align-items:center}
.header{width:100%;background:rgba(0,0,0,.3);padding:15px 40px;display:flex;align-items:center;gap:20px}
.header a{color:#aaa;text-decoration:none}.header a:hover{color:#fff}
.container{max-width:600px;width:100%;padding:40px 20px}
h2{margin-bottom:30px;text-align:center}
.btn{display:block;width:100%;padding:14px;border:none;border-radius:8px;font-size:1.1em;cursor:pointer;margin:10px 0;transition:.2s}
.btn-record{background:#2196F3;color:#fff}.btn-record:hover{background:#1e88e5}
.btn-record.recording{background:#f44336;animation:pulse 1.5s infinite}
.btn-analog{background:#4caf50;color:#fff}.btn-analog:hover{background:#43a047}
.btn-analog.recording{background:#f44336;animation:pulse 1.5s infinite}
.btn-pdm{background:#ff9800;color:#fff}.btn-pdm:hover{background:#f57c00}
.btn-pdm.recording{background:#f44336;animation:pulse 1.5s infinite}
.btn-print{background:#4caf50;color:#fff;display:none}.btn-print:hover{background:#43a047}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.7}}
.result-card{background:rgba(255,255,255,.08);border-radius:16px;padding:25px;margin:20px 0;border:1px solid rgba(255,255,255,.1)}
.result-card .row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid rgba(255,255,255,.05)}
.result-card .label{color:#aaa}.result-card .value{font-weight:bold;font-size:1.1em}
.result-card .category{background:#4caf50;color:#fff;padding:2px 12px;border-radius:12px;font-size:.9em}
.result-card .source{display:inline-block;background:#ff9800;color:#fff;padding:2px 8px;border-radius:10px;font-size:.75em;margin-left:4px}
.msg{padding:12px;border-radius:8px;margin:10px 0;text-align:center}
.msg.err{background:rgba(244,67,54,.2);border:1px solid #f44336}
.msg.info{background:rgba(33,150,243,.2);border:1px solid #2196F3}
.msg.warn{background:rgba(255,152,0,.2);border:1px solid #ff9800}
.spinner{display:none;width:40px;height:40px;margin:20px auto;border:4px solid rgba(255,255,255,.1);border-top-color:#ff9800;border-radius:50%;animation:spin .8s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.timer{text-align:center;font-size:3em;margin:15px 0}
.divider{display:flex;align-items:center;gap:15px;margin:20px 0;color:#666;font-size:.85em}
.divider::before,.divider::after{content:'';flex:1;height:1px;background:rgba(255,255,255,.1)}
.pdm-status{font-size:.8em;color:#aaa;text-align:center;margin-bottom:5px}
</style>
</head>
<body>
<div class="header"><a href="/">&larr; 首页</a><h3>语音书名识别</h3><div style="font-size:.85em;color:#aaa">仅管理员可用</div></div>
<div class="container">
  <!-- 模拟麦克风 -->
  <div class="pdm-status" id="analogStatus2">模拟麦克风: 检测中...</div>
  <button class="btn btn-analog" id="analogRecBtn" onclick="analogRecord()">板载咪头 识别 (5秒)</button>
  <div class="timer" id="analogRecTimer" style="display:none">5</div>

  <div class="spinner" id="spinner"></div>
  <div id="msg"></div>
  <div class="result-card" id="result" style="display:none">
    <h3 style="margin-bottom:15px">识别结果 <span class="source" id="sourceTag"></span></h3>
    <div class="row"><span class="label">说话人</span><span class="value" id="speaker">-</span></div>
    <div class="row"><span class="label">书名</span><span class="value" id="title">-</span></div>
    <div class="row"><span class="label">分类</span><span class="value"><span class="category" id="category">-</span></span></div>
    <button class="btn btn-print" id="printBtn" onclick="printLabel()">&#x1F4E0; 打印标签 + 录入书架</button>
  </div>
</div>
<script>
let mediaRecorder, audioChunks=[], recording=false;

// ---- 模拟麦克风识别 ----
let analogRecording = false;
async function analogRecord() {
  if (analogRecording) return;
  analogRecording = true;
  const btn = document.getElementById('analogRecBtn');
  const timer = document.getElementById('analogRecTimer');
  const spin = document.getElementById('spinner');
  const dur = 5;
  btn.classList.add('recording');
  btn.textContent = '录音中...';
  btn.disabled = true;
  document.getElementById('result').style.display = 'none';
  document.getElementById('msg').innerHTML = '';

  let remain = dur;
  timer.style.display = 'block';
  timer.textContent = remain;
  const ti = setInterval(() => { remain--; timer.textContent = remain; if (remain <= 0) clearInterval(ti); }, 1000);

  try {
    spin.style.display = 'block';
    const resp = await fetch('/api/analog-record', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({duration: dur}),
    });
    clearInterval(ti);
    timer.style.display = 'none';
    spin.style.display = 'none';
    btn.classList.remove('recording');
    btn.textContent = '板载咪头 识别 (5秒)';
    btn.disabled = false;
    analogRecording = false;

    const d = await resp.json();
    if (d.ok) {
      document.getElementById('speaker').textContent = d.speaker;
      document.getElementById('title').textContent = d.title;
      document.getElementById('category').textContent = d.category;
      document.getElementById('sourceTag').textContent = 'ANALOG_MIC';
      document.getElementById('result').style.display = 'block';
      document.getElementById('printBtn').style.display = 'block';
      lastTitle = d.title;
      lastCategory = d.category;
      showMsg('✅ 识别成功', 'info');
    } else {
      showMsg('❌ 失败: ' + (d.error || ''), 'err');
    }
  } catch (e) {
    clearInterval(ti);
    timer.style.display = 'none';
    spin.style.display = 'none';
    btn.classList.remove('recording');
    btn.textContent = '板载咪头 识别 (5秒)';
    btn.disabled = false;
    analogRecording = false;
    showMsg('网络错误: ' + e.message, 'err');
  }
}

// ---- PDM MEMS 麦克风录音 ----
let pdmRecording = false;
async function pdmRecord() {
  if (pdmRecording) return;
  pdmRecording = true;
  const btn = document.getElementById('pdmBtn');
  const timer = document.getElementById('pdmTimer');
  const spin = document.getElementById('spinner');
  const dur = 5;
  btn.classList.add('recording');
  btn.textContent = 'PDM录音中...';
  btn.disabled = true;
  document.getElementById('result').style.display = 'none';
  document.getElementById('msg').innerHTML = '';

  let remain = dur;
  timer.style.display = 'block';
  timer.textContent = remain;
  const ti = setInterval(() => { remain--; timer.textContent = remain; if (remain <= 0) clearInterval(ti); }, 1000);

  try {
    const resp = await fetch('/api/pdm-record', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({duration: dur}),
    });
    clearInterval(ti);
    timer.style.display = 'none';
    btn.classList.remove('recording');
    btn.textContent = 'PDM MEMS 录音 (5秒)';
    btn.disabled = false;
    pdmRecording = false;

    const d = await resp.json();
    if (d.ok) {
      document.getElementById('result').style.display = 'block';
      document.getElementById('sourceTag').textContent = 'PDM MEMS';
      document.getElementById('speaker').textContent = d.speaker || '-';
      document.getElementById('title').textContent = d.title || '-';
      document.getElementById('category').textContent = d.category || '-';
      document.getElementById('printBtn').style.display = 'block';
      bookData = {title: d.title, category: d.category};
    } else {
      showMsg(d.error || '识别失败', d.error && d.error.includes('声纹') ? 'warn' : 'err');
    }
  } catch (e) {
    clearInterval(ti);
    timer.style.display = 'none';
    btn.classList.remove('recording');
    btn.textContent = 'PDM MEMS 录音 (5秒)';
    btn.disabled = false;
    pdmRecording = false;
    showMsg('网络错误: ' + e.message, 'err');
  }
}

// ---- 手机麦克风录音 (备选) ----
async function toggleRecord(){
  if(recording){stopRecord();return}
  try{
    let stream=await navigator.mediaDevices.getUserMedia({audio:true});
    let mime='audio/webm;codecs=opus';
    if(!MediaRecorder.isTypeSupported(mime)) mime='audio/webm';
    mediaRecorder=new MediaRecorder(stream,{mimeType:mime});
    audioChunks=[];
    mediaRecorder.ondataavailable=e=>{if(e.data.size>0) audioChunks.push(e.data)};
    mediaRecorder.onstop=()=>processRecording();
    mediaRecorder.start();
    recording=true;
    document.getElementById('recordBtn').textContent='录音中...';
    document.getElementById('recordBtn').classList.add('recording');
    document.getElementById('timer').style.display='block';
    document.getElementById('result').style.display='none';
    document.getElementById('printBtn').style.display='none';
    document.getElementById('msg').innerHTML='';
    let t=5;document.getElementById('timer').textContent=t;
    let timer=setInterval(()=>{t--;document.getElementById('timer').textContent=t;if(t<=0){clearInterval(timer);stopRecord()}},1000);
  }catch(e){showMsg('麦克风错误: '+e.message,'err')}
}
function stopRecord(){
  if(mediaRecorder&&mediaRecorder.state==='recording') mediaRecorder.stop();
  recording=false;
  document.getElementById('recordBtn').textContent='手机麦克风 (5秒)';
  document.getElementById('recordBtn').classList.remove('recording');
  document.getElementById('timer').style.display='none';
}
async function processRecording(){
  if(audioChunks.length===0){showMsg('未录到音频','err');return}
  showMsg('正在处理音频...','info');
  document.getElementById('spinner').style.display='block';
  try{
    let blob=new Blob(audioChunks);
    let arrayBuf=await blob.arrayBuffer();
    let actx=new AudioContext();
    let audioBuf=await actx.decodeAudioData(arrayBuf);
    await actx.close();
    let pcm;
    if(audioBuf.sampleRate===16000&&audioBuf.numberOfChannels===1){
      pcm=audioBuf.getChannelData(0);
    }else{
      let offline=new OfflineAudioContext(1,Math.ceil(audioBuf.duration*16000),16000);
      let src=offline.createBufferSource();
      src.buffer=audioBuf;
      src.connect(offline.destination);
      src.start();
      let rendered=await offline.startRendering();
      pcm=rendered.getChannelData(0);
    }
    let i16=new Int16Array(pcm.length);
    for(let i=0;i<pcm.length;i++){let s=Math.max(-1,Math.min(1,pcm[i]));i16[i]=s<0?Math.round(s*32768):Math.round(s*32767)}
    let bytes=new Uint8Array(i16.buffer),chunks=[];
    for(let i=0;i<bytes.length;i+=4096)chunks.push(String.fromCharCode(...bytes.slice(i,i+4096)));
    let b64=btoa(chunks.join(''));
    let r=await fetch('/api/recognize',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({b64})});
    let d=await r.json();
    document.getElementById('spinner').style.display='none';
    if(d.ok){
      document.getElementById('result').style.display='block';
      document.getElementById('sourceTag').textContent = '手机';
      document.getElementById('speaker').textContent=d.speaker||'-';
      document.getElementById('title').textContent=d.title||'-';
      document.getElementById('category').textContent=d.category||'-';
      document.getElementById('printBtn').style.display='block';
      bookData={title:d.title,category:d.category};
    }else{
      showMsg(d.error||'识别失败',d.error&&d.error.includes('声纹')?'warn':'err');
    }
  }catch(e){document.getElementById('spinner').style.display='none';showMsg('处理失败: '+e.message,'err')}
}
let bookData=null;
async function printLabel(){
  if(!bookData||!bookData.title){showMsg('没有可打印的书本信息','err');return}
  document.getElementById('printBtn').disabled=true;
  document.getElementById('printBtn').textContent='正在录入...';
  try{
    let r=await fetch('/api/print',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(bookData)});
    let d=await r.json();
    if(d.ok){
      showMsg(d.message||'已录入云端，打印机正在打印标签...','ok');
    }else{
      showMsg('录入失败: '+(d.error||''),'err');
    }
  }catch(e){showMsg('网络错误','err')}
  document.getElementById('printBtn').disabled=false;
  document.getElementById('printBtn').textContent='📰 打印标签 + 录入书架';
}

function showMsg(text,cls){document.getElementById('msg').innerHTML='<div class="msg '+cls+'">'+text+'</div>'}

// Check analog mic status on load
fetch('/api/analog-status').then(r=>r.json()).then(s => {
  document.getElementById('analogStatus2').textContent = s.available
    ? '模拟麦克风: 在线 (RK809)'
    : '模拟麦克风: 离线';
  if (!s.available) {
    document.getElementById('analogRecBtn').disabled = true;
    document.getElementById('analogRecBtn').style.opacity = '0.5';
  }
});
// Check PDM status on load
fetch('/api/pdm-status').then(r=>r.json()).then(s => {
  document.getElementById('pdmStatus').textContent = s.available
    ? 'PDM状态: 在线 | 已采集 ' + (s.total_samples/1000).toFixed(0) + 'k 样本'
    : 'PDM状态: 离线 (RT-Thread未运行)';
  if (!s.available) {
    document.getElementById('pdmBtn').disabled = true;
    document.getElementById('pdmBtn').style.opacity = '0.5';
  }
});
</script>
</body>
</html>'''
# ==================== API 路由 ====================

@app.route("/worklet/processor.js")
def worklet_processor():
    return app.response_class(
        '''class VoiceProcessor extends AudioWorkletProcessor {
  process(inputs, outputs, parameters) {
    const input = inputs[0];
    if (input && input.length > 0 && input[0].length > 0) {
      this.port.postMessage({samples: input[0], nonZero: input[0].filter(v=>v!==0).length});
    }
    // Copy input to output to keep audio graph alive
    const output = outputs[0];
    if (output && output.length > 0) {
      for (let ch = 0; ch < output.length && ch < input.length; ch++) {
        output[ch].set(input[ch]);
      }
    }
    return true;
  }
}
registerProcessor("voice-processor", VoiceProcessor);''',
        mimetype='application/javascript'
    )

@app.route("/")
def index():
    return render_template_string(INDEX_HTML, vp_ok=vp is not None)

@app.route("/enroll")
def enroll():
    return render_template_string(ENROLL_HTML)

@app.route("/recognize")
def recognize():
    return render_template_string(RECOGNIZE_HTML)

@app.route("/api/voices")
def list_voices():
    if vp is None:
        return jsonify({"names": [], "error": "声纹模型未加载"})
    names = vp["manager"].all_speakers
    return jsonify({"names": names})

@app.route("/api/enroll", methods=["POST"])
def api_enroll():
    if vp is None:
        return jsonify({"ok": False, "error": "声纹模型未加载"})

    data = request.json
    name = data.get("name", "").strip()
    if not name:
        return jsonify({"ok": False, "error": "姓名不能为空"})

    try:
        ext = data.get("ext", "wav")
        audio = decode_audio(data["b64"])
        print(f"[Enroll] ext={ext} samples={len(audio)} min={audio.min():.4f} max={audio.max():.4f} mean_abs={abs(audio).mean():.4f}", flush=True)
        emb = extract_embedding(audio)
        if emb is None:
            return jsonify({"ok": False, "error": f"未检测到有效语音 (samples={len(audio)}, mean_abs={abs(audio).mean():.4f})"})

        np.save(os.path.join(VOICEPRINT_DIR, f"{name}.npy"), emb)
        vp["manager"].add(name, emb)
        return jsonify({"ok": True, "name": name, "count": vp["manager"].num_speakers})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})

@app.route("/api/print", methods=["POST"])
def api_print():
    """录入书本到本地主控 API + 触发打印"""
    data = request.json
    title = data.get("title", "").strip()
    category = data.get("category", "其他类")
    if not title:
        return jsonify({"ok": False, "error": "书名为空"})
    try:
        import requests as req
        resp = req.post(
            "http://127.0.0.1:9777/api/book/add",
            json={"author": "管理员", "category": category, "title": title},
            headers={"Content-Type": "application/json"},
            timeout=15
        )
        if resp.status_code == 200:
            j = resp.json()
            if j.get("code") == 200:
                return jsonify({"ok": True, "message": f"《{title}》已录入 {category}，打印机正在工作..."})
        print(f"[Print] API error: {resp.status_code} {resp.text[:200]}", flush=True)
        return jsonify({"ok": False, "error": f"云端录入失败 (HTTP {resp.status_code})"})
    except Exception as e:
        print(f"[Print] error: {e}", flush=True)
        return jsonify({"ok": False, "error": str(e)})

@app.route("/api/recognize", methods=["POST"])
def api_recognize():
    if vp is None:
        return jsonify({"ok": False, "error": "声纹模型未加载"})

    data = request.json
    try:
        ext = data.get("ext", "wav")
        audio = decode_audio(data["b64"])

        # Step 1: 声纹验证
        emb = extract_embedding(audio)
        if emb is None:
            return jsonify({"ok": False, "error": "未检测到有效语音"})

        if vp["manager"].num_speakers == 0:
            return jsonify({"ok": False, "error": "声纹库为空，请先注册管理员声纹"})

        name, score = search_voiceprint(emb)
        threshold = 0.5
        if score < threshold:
            return jsonify({"ok": False, "error": f"声纹验证失败：不是管理员语音 (最佳匹配: {name}, 分数: {score:.2f})"})

        # Step 2: 上传到OSS + ASR
        print(f'[Recognize] uploading audio {len(audio)} samples', flush=True)
        try:
            audio_url = upload_to_oss(audio)
            print(f'[Recognize] OSS: {audio_url}', flush=True)
        except Exception as e:
            print(f'[Recognize] OSS error: {e}', flush=True)
            return jsonify({"ok": False, "error": f"OSS上传失败: {e}"})
        try:
            raw_text = call_asr(audio_url)
            print(f'[Recognize] ASR text: {repr(raw_text)}', flush=True)
        except Exception as e:
            print(f'[Recognize] ASR error: {e}', flush=True)
            return jsonify({"ok": False, "error": f"ASR调用失败: {e}"})
        if not raw_text:
            return jsonify({"ok": False, "error": "ASR识别失败"})

        # Step 3: DeepSeek 分类
        title = extract_title(raw_text)
        category = classify_title(title)

        return jsonify({
            "ok": True,
            "speaker": name,
            "raw": raw_text,
            "title": title,
            "category": category,
            "score": round(score, 3),
        })
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})

@app.route("/api/pdm-record", methods=["POST"])
def api_pdm_record():
    """RT-Thread PDM MEMS麦克风录音 → 声纹验证 → ASR → 分类"""
    if not PDM_AVAILABLE:
        return jsonify({"ok": False, "error": "PDM麦克风不可用，RT-Thread PDM固件未运行"})

    data = request.get_json(silent=True) or {}
    duration = min(float(data.get("duration", 5)), 10)

    try:
        audio_16k = pdm_capture_16k(duration)
        if len(audio_16k) < 500:
            return jsonify({"ok": False, "error": f"PDM录音太短 ({len(audio_16k)} samples)"})

        print(f"[PDM-Record] {len(audio_16k)}s@16kHz "
              f"min={audio_16k.min():.4f} max={audio_16k.max():.4f} mean_abs={abs(audio_16k).mean():.4f}", flush=True)
    except Exception as e:
        return jsonify({"ok": False, "error": f"PDM录音失败: {e}"})

    # 声纹验证
    speaker = "未启用声纹"
    if vp:
        emb = extract_embedding(audio_16k)
        if emb is None:
            return jsonify({"ok": False, "error": "未检测到有效语音"})
        if vp["manager"].num_speakers == 0:
            return jsonify({"ok": False, "error": "声纹库为空，请先注册管理员声纹"})
        name, score = search_voiceprint(emb)
        if score < 0.5:
            return jsonify({"ok": False, "error": f"声纹验证未通过 (最佳匹配:{name}, 分数:{score:.2f})"})
        speaker = name

    # 保存到 /userdata/audio/
    audio_dir = "/userdata/audio"
    os.makedirs(audio_dir, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    wav_path = os.path.join(audio_dir, f"pdm_{ts}.wav")
    sf.write(wav_path, audio_16k, 16000)
    print(f"[PDM-Record] saved {wav_path}", flush=True)

    # ASR + 分类
    try:
        audio_url = upload_to_oss(audio_16k)
        raw_text = call_asr(audio_url)
        if not raw_text:
            return jsonify({"ok": False, "error": "ASR识别失败"})
        title = extract_title(raw_text)
        category = classify_title(title)
    except Exception as e:
        return jsonify({"ok": False, "error": f"ASR/分类错误: {e}"})

    return jsonify({
        "ok": True,
        "speaker": speaker,
        "raw": raw_text,
        "title": title,
        "category": category,
        "audio_file": wav_path,
        "source": "PDM_MEMS",
    })


@app.route("/api/pdm-enroll", methods=["POST"])
def api_pdm_enroll():
    """PDM麦克风声纹注册"""
    if not PDM_AVAILABLE:
        return jsonify({"ok": False, "error": "PDM麦克风不可用"})
    if vp is None:
        return jsonify({"ok": False, "error": "声纹模型未加载"})

    data = request.get_json(silent=True) or {}
    name = (data.get("name") or "").strip()
    if not name:
        return jsonify({"ok": False, "error": "姓名不能为空"})
    duration = min(float(data.get("duration", 5)), 10)

    try:
        audio_16k = pdm_capture_16k(duration)
        print(f"[PDM-Enroll] {name}: {len(audio_16k)}s@16kHz "
              f"min={audio_16k.min():.4f} max={audio_16k.max():.4f} mean_abs={abs(audio_16k).mean():.4f}", flush=True)
    except Exception as e:
        return jsonify({"ok": False, "error": f"PDM录音失败: {e}"})

    emb = extract_embedding(audio_16k)
    if emb is None:
        return jsonify({"ok": False, "error": "未检测到有效语音，请对准PDM麦克风清晰说话"})

    np.save(os.path.join(VOICEPRINT_DIR, f"{name}.npy"), emb)
    vp["manager"].add(name, emb)
    return jsonify({"ok": True, "name": name, "count": vp["manager"].num_speakers})


@app.route("/api/login", methods=["POST"])
def api_voice_login():
    """PDM声纹登录：录音→提取声纹→匹配→返回说话人"""
    if not PDM_AVAILABLE:
        return jsonify({"ok": False, "error": "PDM麦克风不可用"})
    if vp is None:
        return jsonify({"ok": False, "error": "声纹模型未加载"})
    if vp["manager"].num_speakers == 0:
        return jsonify({"ok": False, "error": "声纹库为空，请先注册管理员声纹"})

    data = request.get_json(silent=True) or {}
    duration = min(float(data.get("duration", 3)), 5)

    try:
        audio_16k = pdm_capture_16k(duration)
        if len(audio_16k) < 500:
            return jsonify({"ok": False, "error": f"录音太短 ({len(audio_16k)} samples)"})
    except Exception as e:
        return jsonify({"ok": False, "error": f"PDM录音失败: {e}"})

    emb = extract_embedding(audio_16k)
    if emb is None:
        return jsonify({"ok": False, "error": "未检测到有效语音"})

    name, score = search_voiceprint(emb)
    if score < 0.5:
        return jsonify({"ok": False, "error": f"声纹验证未通过", "best_match": name, "score": round(score, 3)})

    return jsonify({"ok": True, "speaker": name, "score": round(score, 3)})


@app.route("/api/delete", methods=["POST"])
def api_voice_delete():
    """删除声纹"""
    if vp is None:
        return jsonify({"ok": False, "error": "声纹模型未加载"})

    data = request.get_json(silent=True) or {}
    name = (data.get("name") or "").strip()
    if not name:
        return jsonify({"ok": False, "error": "名称为空"})

    path = os.path.join(VOICEPRINT_DIR, f"{name}.npy")
    if not os.path.exists(path):
        return jsonify({"ok": False, "error": f"声纹 '{name}' 不存在"})

    os.remove(path)
    # Reload all voiceprints
    new_manager = sherpa_onnx.SpeakerEmbeddingManager(vp["extractor"].dim)
    for f in Path(VOICEPRINT_DIR).glob("*.npy"):
        try:
            new_manager.add(f.stem, np.load(str(f)))
        except Exception:
            pass
    vp["manager"] = new_manager
    return jsonify({"ok": True, "deleted": name, "count": vp["manager"].num_speakers})


@app.route("/api/pdm-status")
def api_pdm_status():
    """检查 PDM 麦克风状态 (devmem, no mmap)"""
    if PDM_AVAILABLE:
        try:
            cnt = subprocess.check_output(
                ["devmem", "0x48418008", "32"],
                stderr=subprocess.DEVNULL, timeout=3,
            ).decode().strip()
            total = int(cnt, 16) if cnt.startswith("0x") else 0
            return jsonify({"available": True, "total_samples": total, "rate": 8000})
        except Exception:
            pass
    return jsonify({"available": False})


# ===== ASR 生命周期管理 =====

def _unload_asr():
    """卸载 ASR 模型，释放 ~517MB"""
    import asr_engine
    if asr_engine._asr is not None:
        asr_engine._asr = None
        gc.collect()
        print(f"[ASR] Unloaded, RSS≈{_rss_mb():.0f}MB", flush=True)


def _reload_asr_background():
    """后台线程热重载 ASR（page cache 命中，~5-8s）"""
    def _load():
        import asr_engine
        print("[ASR] Background reload started...", flush=True)
        asr_engine.get_asr()
        print(f"[ASR] Reloaded, RSS≈{_rss_mb():.0f}MB", flush=True)

    t = threading.Thread(target=_load, daemon=True)
    t.start()


def _rss_mb():
    """当前进程 RSS (MB)"""
    try:
        with open("/proc/self/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024
    except Exception:
        return 0


@app.route("/api/preload", methods=["POST"])
def api_preload():
    """管理员登录后预热：加载 ASR 模型（云端 AI 链无需预热 LLM）"""
    import asr_engine

    def _warm():
        try:
            asr = asr_engine.get_asr()
            print(f"[Preload] ASR ready", flush=True)
        except Exception as e:
            print(f"[Preload] ASR failed: {e}", flush=True)

    t = threading.Thread(target=_warm, daemon=True)
    t.start()
    return jsonify({"ok": True, "msg": "ASR 预热已启动"})


@app.route("/api/analog-status")
def api_analog_status():
    return jsonify({"available": ANALOG_MIC_AVAILABLE, "device": "hw:1,0", "codec": "RK809"})


@app.route("/api/analog-record", methods=["POST"])
def api_analog_record():
    """模拟麦克风录音 → 声纹验证 → ASR → 分类"""
    if not ANALOG_MIC_AVAILABLE:
        return jsonify({"ok": False, "error": "模拟麦克风不可用，请检查咪头是否插入AUDIO口"})

    data = request.get_json(silent=True) or {}
    duration = min(float(data.get("duration", 5)), 10)
    skip_vp = data.get("skip_voiceprint", False)
    login_only = data.get("login_only", False)  # 声纹解锁——只验证声纹，不走 AI 链

    try:
        audio_16k = analog_capture_16k(duration)
        if len(audio_16k) < 500:
            return jsonify({"ok": False, "error": f"录音太短 ({len(audio_16k)} samples)"})

        print(f"[Analog-Record] {len(audio_16k)}s@16kHz "
              f"min={audio_16k.min():.4f} max={audio_16k.max():.4f} "
              f"rms={np.sqrt((audio_16k**2).mean()):.4f}", flush=True)
    except Exception as e:
        return jsonify({"ok": False, "error": f"模拟录音失败: {e}"})

    # 声纹验证（仅语音解锁需要；语音录入跳过）
    speaker = "语音录入"
    if not skip_vp and vp:
        emb = extract_embedding(audio_16k)
        if emb is None:
            return jsonify({"ok": False, "error": "未检测到有效语音"})
        if vp["manager"].num_speakers == 0:
            return jsonify({"ok": False, "error": "声纹库为空，请先注册管理员声纹"})
        name, score = search_voiceprint(emb)
        if score < 0.5:
            return jsonify({"ok": False, "error": f"声纹验证未通过 (最佳匹配:{name}, 分数:{score:.2f})"})
        speaker = name
        # 声纹解锁：验证通过直接返回，不需要走 AI 文字识别
        if login_only:
            return jsonify({"ok": True, "speaker": speaker, "score": round(score, 3)})

    # 保存录音
    audio_dir = "/userdata/audio"
    os.makedirs(audio_dir, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    wav_path = os.path.join(audio_dir, f"analog_{ts}.wav")
    sf.write(wav_path, audio_16k, 16000)
    print(f"[Analog-Record] saved {wav_path}", flush=True)

    # 云端 AI 管线: ASR(本地) → DeepSeek 提取书名+分类 → 书库+Serper 验证
    import time as _time
    trace = {"asr": {}, "llm": {}, "pipeline": {}, "timings": {}}
    t_start = _time.time()
    try:
        # 1. 本地 ASR (SenseVoice, 常驻内存, 不再卸载——无需给本地 LLM 腾内存)
        t0 = _time.time()
        try:
            from asr_engine import get_asr
            raw_text = get_asr().transcribe(audio_16k)
            trace["asr"] = {"engine": "SenseVoice", "text": raw_text}
            print(f"[Local-ASR] {raw_text}", flush=True)
        except Exception as asr_e:
            print(f"[Local-ASR] unavailable: {asr_e}, falling back to cloud", flush=True)
            audio_url = upload_to_oss(audio_16k)
            raw_text = call_asr(audio_url)
            trace["asr"] = {"engine": "DashScope", "text": raw_text}
        trace["timings"]["asr_ms"] = int((_time.time()-t0)*1000)

        if not raw_text:
            trace["error"] = "ASR识别失败"
            return jsonify({"ok": False, "error": "ASR识别失败", "trace": trace})

        # 2. DeepSeek v4-pro 提取书名 + 分类 (云端大模型, 不再使用本地 Qwen3)
        title = raw_text
        category = "其他"
        t0 = _time.time()
        try:
            title = extract_title(raw_text)
            category = classify_title(title)
            trace["llm"] = {"engine": "DeepSeek-v4-pro", "title": title, "category": category}
            if not title:
                title = raw_text[:30]
        except Exception as llm_e:
            print(f"[DeepSeek] API error: {llm_e}", flush=True)
            trace["llm"] = {"engine": "DeepSeek-error", "error": str(llm_e)}
            title = raw_text[:30]
        trace["timings"]["llm_ms"] = int((_time.time()-t0)*1000)

    except Exception as e:
        return jsonify({"ok": False, "error": f"AI管线错误: {e}", "trace": trace})

    # 3. 智能联动管线: DeepSeek结果 → 书库精确 → 书库模糊 → Serper → 二次书库验证
    try:
        from pipeline import process_voice_result
        t0 = _time.time()
        pip = process_voice_result(raw_text, title, category)
        trace["timings"]["pipeline_ms"] = int((_time.time()-t0)*1000)
        trace["pipeline"] = {"source": pip["source"], "verified": pip["verified"], "warning": pip.get("warning","")}
        trace["timings"]["total_ms"] = int((_time.time()-t_start)*1000)

        return jsonify({
            "ok": True,
            "speaker": speaker,
            "raw": raw_text,
            "title": pip["title"],
            "category": pip["category"],
            "audio_file": wav_path,
            "source": f"CLOUD_AI|{pip['source']}",
            "verified": pip["verified"],
            "confidence": pip["confidence"],
            "warning": pip.get("warning", ""),
            "serper_corrected": pip.get("serper_corrected", False),
            "trace": trace,
        })
    except Exception as e:
        print(f"[Pipeline] unavailable: {e}, using DeepSeek result directly", flush=True)
        from book_index import get_index
        book = get_index().lookup(title)
        if book:
            title = book.get("title", title)
            category = book.get("category", category)
        trace["pipeline"] = {"fallback": str(e)}
        trace["timings"]["total_ms"] = int((_time.time()-t_start)*1000)
        return jsonify({
            "ok": True,
            "speaker": speaker,
            "raw": raw_text,
            "title": title,
            "category": category,
            "audio_file": wav_path,
            "source": "CLOUD_AI",
            "trace": trace,
            "verified": bool(book),
        })


@app.route("/api/analog-enroll", methods=["POST"])
def api_analog_enroll():
    """模拟麦克风声纹注册"""
    if not ANALOG_MIC_AVAILABLE:
        return jsonify({"ok": False, "error": "模拟麦克风不可用"})
    if vp is None:
        return jsonify({"ok": False, "error": "声纹模型未加载"})

    data = request.get_json(silent=True) or {}
    name = (data.get("name") or "").strip()
    if not name:
        return jsonify({"ok": False, "error": "缺少姓名"})
    duration = min(float(data.get("duration", 5)), 10)

    try:
        audio_16k = analog_capture_16k(duration)
        emb = extract_embedding(audio_16k)
        if emb is None:
            return jsonify({"ok": False, "error": "未检测到有效语音，请靠近咪头重试"})

        np.save(os.path.join(VOICEPRINT_DIR, f"{name}.npy"), emb)
        vp["manager"].add(name, emb)

        # 保存录音用于质量检查
        audio_dir = "/userdata/audio"
        os.makedirs(audio_dir, exist_ok=True)
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        wav_path = os.path.join(audio_dir, f"analog_enroll_{name}_{ts}.wav")
        sf.write(wav_path, audio_16k, 16000)

        print(f"[Analog-Enroll] {name} registered, {vp['manager'].num_speakers} speakers", flush=True)
        return jsonify({"ok": True, "name": name, "count": vp["manager"].num_speakers})
    except Exception as e:
        return jsonify({"ok": False, "error": f"注册失败: {e}"})


# ==================== 音频处理 ====================

def decode_audio(b64):
    """Browser PCM16 (16kHz mono) base64 -> numpy float32"""
    data = base64.b64decode(b64)
    samples = np.frombuffer(data, dtype=np.int16)
    audio = samples.astype(np.float32) / 32768.0
    return audio


def extract_embedding(audio):
    """提取声纹嵌入向量"""
    if vp is None:
        return None
    if audio.dtype != np.float32:
        audio = audio.astype(np.float32)
    if len(audio) < 1600:  # < 100ms
        return None
    stream = vp["extractor"].create_stream()
    stream.accept_waveform(16000, audio)
    emb = vp["extractor"].compute(stream)
    return emb

def search_voiceprint(emb):
    """搜索最匹配的声纹"""
    if vp is None or vp["manager"].num_speakers == 0:
        return None, 0
    name = vp["manager"].search(emb, threshold=-1.0)
    if name is None or name not in vp["manager"].all_speakers:
        return "unknown", 0
    score = vp["manager"].score(name, emb)
    return name, score

# ==================== 云端API ====================

import oss2, dashscope, requests

oss_auth = oss2.Auth(OSS_KEY_ID, OSS_KEY_SECRET)
oss_bucket = oss2.Bucket(oss_auth, OSS_ENDPOINT, OSS_BUCKET)
dashscope.api_key = DASHSCOPE_KEY

def upload_to_oss(audio):
    """上传音频到OSS，返回签名HTTPS URL"""
    if audio.dtype != np.int16:
        audio = (audio * 32767).astype(np.int16)
    buf = io.BytesIO()
    sf.write(buf, audio, 16000, format="WAV", subtype="PCM_16")
    buf.seek(0)
    ts = int(time.time() * 1000)
    key = f"audio/voice_{ts}.wav"
    oss_bucket.put_object(key, buf.getvalue())
    signed = oss_bucket.sign_url('GET', key, 3600, slash_safe=True)
    return signed.replace('http://', 'https://')

def call_asr(audio_url):
    """ASR via DashScope Transcription API (paraformer-v1)"""
    import requests
    from dashscope.audio.asr import Transcription
    try:
        resp = Transcription.call(model="paraformer-v1", file_urls=[audio_url])
        print(f"[ASR] status={resp.status_code} output_type={type(resp.output).__name__}", flush=True)

        if resp.status_code != 200:
            print(f"[ASR] Error: {resp.message}", flush=True)
            return None

        out = resp.output
        # Handle both dict and object response types
        if out is None:
            print("[ASR] output is None", flush=True)
            return None

        # Try extracting results
        results = None
        if hasattr(out, "get") and callable(out.get):
            results = out.get("results", [])
        elif hasattr(out, "results"):
            results = out.results
        elif isinstance(out, dict):
            results = out.get("results", [])
        else:
            # Unknown type — dump all attributes
            print(f"[ASR] Unknown output type, attrs: {[a for a in dir(out) if not a.startswith('_')]}", flush=True)
            return None

        print(f"[ASR] results count: {len(results) if results else 0}", flush=True)

        if not results:
            return None

        for r in results:
            # Handle both dict and object item types
            get = r.get if hasattr(r, "get") else lambda k, d=None: getattr(r, k, d)
            st = get("subtask_status", "") or get("task_status", "")
            turl = get("transcription_url", "")
            print(f"[ASR] subtask: status={st} has_url={bool(turl)}", flush=True)

            if st == "SUCCEEDED" and turl:
                tresp = requests.get(turl, timeout=10)
                if tresp.status_code == 200:
                    data = tresp.json()
                    text = data.get("transcripts", [{}])[0].get("text", "")
                    if text:
                        print(f"[ASR] text={repr(text)}", flush=True)
                        return text.strip()
                else:
                    print(f"[ASR] transcription_url HTTP {tresp.status_code}", flush=True)

        print("[ASR] no text extracted from results", flush=True)
        return None
    except Exception as e:
        import traceback
        print(f"[ASR] error: {e}", flush=True)
        traceback.print_exc()
        return None

def call_deepseek(prompt, max_tokens=50):
    """DeepSeek v4-pro API"""
    resp = requests.post(
        "https://api.deepseek.com/v1/chat/completions",
        headers={"Authorization": f"Bearer {DEEPSEEK_KEY}"},
        json={"model": "deepseek-chat", "messages": [{"role":"user","content":prompt}],
              "max_tokens": max_tokens, "temperature": 0.1},
        timeout=15,
    )
    if resp.status_code == 200:
        return resp.json()["choices"][0]["message"]["content"].strip()
    return None

def extract_title(raw_text):
    """DeepSeek提取书名"""
    prompt = f"请从以下文本中提取书名，只输出书名，不要有其他内容。如果文本中没有书名，直接输出原文。\n\n文本：{raw_text}"
    title = call_deepseek(prompt, 30)
    return title or raw_text[:30]

def classify_title(title):
    """DeepSeek v4-pro 分类 (四分类: 教辅/文学/科技/其他)"""
    cats = "、".join(CATEGORIES)
    prompt = (
        f"请将以下书名归类为以下类别之一：{cats}。\n"
        f"教辅: 教材/辅导书/真题/考试/启蒙/绘本/工具书/练习册\n"
        f"文学: 小说/诗歌/散文/科幻/奇幻/漫画/轻小说/传记/武侠/言情\n"
        f"科技: 科普/编程/医学/工程/自然科学/互联网/AI/计算机\n"
        f"其他: 历史/哲学/社科/经济/艺术/生活/旅行/健康/心理/励志\n"
        f"只需返回类别名称（如：教辅），不要有其他内容。\n\n书名：{title}"
    )
    cat = call_deepseek(prompt, 15)
    if cat:
        for c in CATEGORIES:
            if c in cat:
                return c
    return CATEGORIES[-1]

# ==================== 启动 ====================

if __name__ == "__main__":
    print(f"Voice AI Debug Platform starting on port 8890")
    print(f"Voiceprint model: {'LOADED' if vp else 'NOT LOADED (download model first)'}")
    print(f"Voiceprints: {vp['manager'].num_speakers if vp else 0} registered")
    # 默认在 loopback 上以 HTTP 提供服务（主控 C++ 代理直连）；设 VOICE_TLS=1 可启用 HTTPS
    use_tls = os.environ.get("VOICE_TLS", "0") == "1"
    ssl_ctx = (CERT_FILE, KEY_FILE) if use_tls else None
    socketio.run(app, host="0.0.0.0", port=8890, debug=False, allow_unsafe_werkzeug=True, ssl_context=ssl_ctx)
