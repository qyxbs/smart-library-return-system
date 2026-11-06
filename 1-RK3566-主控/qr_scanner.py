#!/usr/bin/env python3
"""
OV5695 摄像头 QR 码扫描服务

持续从摄像头采集图像 → 检测 QR 码 → 解析 task_id → 调用主控 API 执行任务

部署:
  python3 qr_scanner.py --camera 0 --api http://127.0.0.1:9777
  或在 S99k230 同级添加 S99qrscanner init 脚本
"""
import os
import sys
import json
import time
import signal
import argparse
import logging
import subprocess
import threading
import requests

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [QR] %(message)s",
)
logger = logging.getLogger("qr_scanner")

# ==================== Config ====================
DEFAULT_CAMERA = 1            # /dev/video1 (OV5695 via rkisp)
DEFAULT_API = "http://127.0.0.1:9777"
SCAN_INTERVAL = 0.15          # seconds between frames (~6.7 fps, fast response)
DUPLICATE_COOLDOWN = 5.0      # seconds before same QR is re-scanned
QR_MIN_SIZE = 100             # minimum QR code size in pixels (ignore too-small detections)

# ==================== State ====================
_scanned_cache = {}  # task_id → last scanned time (anti-duplicate)
_running = False


def _fix_camera_exposure(dev: int = 1):
    """Fix OV5695 auto-exposure runaway: sets exposure + gain to prevent black frames.

    Root cause: rkisp AE algorithm drives exposure to minimum (4) and gain to
    minimum (16), resulting in completely black frames after ~20 warmup frames.
    Fix: force exposure=1104 (mid-range) and analogue_gain=200 (reasonable gain).

    Must be called periodically (~every 30 frames) because AE continuously fights it.
    """
    try:
        subprocess.run(
            ["v4l2-ctl", "-d", f"/dev/video{dev}",
             "-c", "exposure=1104", "-c", "analogue_gain=200"],
            timeout=2, check=False, capture_output=True,
        )
    except Exception:
        pass  # non-critical, just means frames might go dark


def init_camera(index: int = 0):
    """Initialize OpenCV camera capture. Returns cv2.VideoCapture or None."""
    try:
        import cv2
        cap = cv2.VideoCapture(index)
        if not cap.isOpened():
            logger.error(f"Cannot open camera /dev/video{index}")
            return None

        # Configure for OV5695
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        cap.set(cv2.CAP_PROP_FPS, 15)
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 3)  # Must be >1 for V4L2 continuous capture

        logger.info(f"Camera /dev/video{index} ready: {cap.get(cv2.CAP_PROP_FRAME_WIDTH):.0f}x"
                    f"{cap.get(cv2.CAP_PROP_FRAME_HEIGHT):.0f} @ {cap.get(cv2.CAP_PROP_FPS):.0f}fps")
        return cap
    except ImportError:
        logger.error("OpenCV (cv2) not installed. Run: pip3 install opencv-python-headless")
        return None
    except Exception as e:
        logger.error(f"Camera init error: {e}")
        return None


_qr_detector = None  # Singleton: create once, avoid per-frame memory leak

def detect_qr(frame) -> str | None:
    """Detect QR code in frame. Returns decoded text or None."""
    global _qr_detector
    try:
        import cv2
        if _qr_detector is None:
            _qr_detector = cv2.QRCodeDetector()
        data, points, _ = _qr_detector.detectAndDecode(frame)
        if data and points is not None and len(points) > 0:
            if len(points) >= 4:
                w = max(abs(points[0][0][0] - points[0][1][0]),
                        abs(points[0][0][0] - points[0][3][0]))
                if w < QR_MIN_SIZE:
                    return None
            return data.strip()
    except Exception as e:
        logger.debug(f"QR detect error: {e}")
    return None


def parse_qr_content(qr_text: str) -> dict | None:
    """Parse QR content JSON. Returns {task_id, action, ts} or None."""
    try:
        data = json.loads(qr_text)
        task_id = data.get("t", "")
        action = data.get("a", "")
        ts = data.get("ts", 0)
        if not task_id or not action:
            return None
        return {"task_id": task_id, "action": action, "ts": ts}
    except json.JSONDecodeError:
        # Maybe it's a direct task_id
        if len(qr_text) == 12 and qr_text.isalnum():
            return {"task_id": qr_text, "action": "unknown", "ts": 0}
        return None


def submit_to_api(api_base: str, qr_text: str) -> dict:
    """Submit QR content to main API endpoint. Returns API response dict."""
    try:
        resp = requests.post(
            f"{api_base}/api/qr/scan",
            json={"qrContent": qr_text},
            timeout=10,
        )
        return resp.json()
    except requests.ConnectionError:
        logger.error(f"Cannot connect to API at {api_base}")
        return {"code": 500, "message": "API 服务不可达"}
    except Exception as e:
        logger.error(f"API call error: {e}")
        return {"code": 500, "message": str(e)}


def is_duplicate(task_id: str) -> bool:
    """Check if this task_id was recently scanned (anti-duplicate)."""
    now = time.time()
    last = _scanned_cache.get(task_id, 0)
    if now - last < DUPLICATE_COOLDOWN:
        return True
    _scanned_cache[task_id] = now
    # Clean old entries
    for tid in list(_scanned_cache.keys()):
        if now - _scanned_cache[tid] > DUPLICATE_COOLDOWN * 2:
            del _scanned_cache[tid]
    return False


def run_scanner(camera_index: int = 0, api_base: str = DEFAULT_API):
    """Main loop: continuous capture with BUFFERSIZE=4 to prevent V4L2 buffer exhaustion."""
    global _running
    import cv2

    def _handle_signal(sig, frame):
        global _running
        logger.info(f"Shutting down (signal {sig})...")
        _running = False
    signal.signal(signal.SIGTERM, _handle_signal)
    signal.signal(signal.SIGINT, _handle_signal)

    cap = cv2.VideoCapture(camera_index)
    if not cap.isOpened():
        logger.error(f"Cannot open camera /dev/video{camera_index}")
        return
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    # BUFFERSIZE=4 is CRITICAL: V4L2/rkisp needs >=4 buffers for continuous capture
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 4)
    cap.read()  # discard stale first frame

    # Fix OV5695 auto-exposure runaway: prevent frames going black
    _fix_camera_exposure(camera_index)
    # Discard transition frames while exposure stabilizes (~1s warmup)
    for _ in range(25):
        cap.read()
        time.sleep(0.04)

    _running = True
    logger.info(f"QR Scanner started. API: {api_base}")
    print(f"[QR Scanner] Running — show QR code to camera", flush=True)

    frame_count = 0
    while _running:
        try:
            ret, frame = cap.read()
            if not ret:
                time.sleep(0.1)
                continue

            frame_count += 1
            # Re-assert exposure every ~30 frames to prevent AE runaway
            if frame_count % 30 == 0:
                _fix_camera_exposure(camera_index)

            qr_text = detect_qr(frame)
            if qr_text:
                parsed = parse_qr_content(qr_text)
                if parsed and not is_duplicate(parsed["task_id"]):
                    logger.info(f"QR detected: task_id={parsed['task_id']} action={parsed['action']}")
                    print(f"[QR Scanner] Detected: {parsed['task_id']}", flush=True)
                    result = submit_to_api(api_base, qr_text)
                    if result.get("code") == 200:
                        logger.info(f"Task {parsed['task_id']} executed successfully")
                        print(f"[QR Scanner] ✓ Task {parsed['task_id']} done", flush=True)
                    else:
                        print(f"[QR Scanner] ✗ {result.get('message', 'error')}", flush=True)

            time.sleep(SCAN_INTERVAL)

        except Exception as e:
            logger.error(f"Scanner loop error: {e}")
            time.sleep(1)

    cap.release()
    logger.info("QR Scanner stopped")


def stop_scanner():
    global _running
    _running = False


def get_status() -> dict:
    return {
        "running": _running,
        "camera": f"/dev/video{DEFAULT_CAMERA}",
        "scanned_count": len(_scanned_cache),
    }


# ==================== Init Script Support ====================

def start():
    """Entry point for init script / manual start."""
    run_scanner()


def stop():
    stop_scanner()


# ==================== Main ====================

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="QR Code Scanner for Smart Bookshelf")
    parser.add_argument("--camera", type=int, default=DEFAULT_CAMERA,
                        help=f"Camera device index (default: {DEFAULT_CAMERA})")
    parser.add_argument("--api", default=DEFAULT_API,
                        help=f"Main API base URL (default: {DEFAULT_API})")
    parser.add_argument("--interval", type=float, default=SCAN_INTERVAL,
                        help=f"Frame interval in seconds (default: {SCAN_INTERVAL})")
    parser.add_argument("--once", action="store_true",
                        help="Scan once and exit (for testing)")
    args = parser.parse_args()

    SCAN_INTERVAL = args.interval

    if args.once:
        cap = init_camera(args.camera)
        if cap is None:
            sys.exit(1)
        import cv2
        print("Scanning... (show QR code to camera, Ctrl+C to exit)", flush=True)
        try:
            while True:
                ret, frame = cap.read()
                if not ret:
                    time.sleep(0.1)
                    continue
                qr_text = detect_qr(frame)
                if qr_text:
                    print(f"Detected: {qr_text}", flush=True)
                    parsed = parse_qr_content(qr_text)
                    if parsed:
                        result = submit_to_api(args.api, qr_text)
                        print(f"API response: {json.dumps(result, ensure_ascii=False)}", flush=True)
                        break
                time.sleep(0.5)
        except KeyboardInterrupt:
            pass
        cap.release()
    else:
        try:
            run_scanner(args.camera, args.api)
        except KeyboardInterrupt:
            stop_scanner()
            print("\n[QR Scanner] Stopped", flush=True)
