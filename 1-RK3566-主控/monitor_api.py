"""
System monitor API — adds /api/sysmon/* endpoints to Flask app
Import and call register_sysmon_routes(app) in app.py
"""
import os
import subprocess
import glob

LOG_DIR = "/userdata/log"
SERVICE_PIDFILES = {
    "book_app": "/var/run/book_app.pid",
    "k230_bridge": "/var/run/k230_bridge.pid",
    "hostapd": "/var/run/hostapd.pid",
    "dnsmasq": "/var/run/dnsmasq.pid",
}


def _check_pid(pidfile):
    """Return (running:bool, pid:int|None)"""
    if not os.path.exists(pidfile):
        return False, None
    try:
        with open(pidfile) as f:
            pid = int(f.read().strip())
        os.kill(pid, 0)
        return True, pid
    except (ValueError, OSError):
        return False, None


def _get_wifi_status():
    """Get WiFi interface status."""
    result = {}
    for iface in ["wlan0", "wlan1"]:
        if not os.path.exists(f"/sys/class/net/{iface}"):
            continue
        info = {}
        # SSID and type
        try:
            out = subprocess.check_output(
                ["iw", "dev", iface, "info"], stderr=subprocess.DEVNULL, timeout=3
            ).decode()
            for line in out.split("\n"):
                line = line.strip()
                if line.startswith("ssid "):
                    info["ssid"] = line.split(maxsplit=1)[1]
                elif line.startswith("type "):
                    info["type"] = line.split(maxsplit=1)[1]
                elif line.startswith("channel "):
                    info["channel"] = line.split()[1]
                elif line.startswith("txpower "):
                    info["txpower"] = line.split()[1]
        except Exception:
            pass
        # IP
        try:
            out = subprocess.check_output(
                ["ip", "-4", "addr", "show", iface], stderr=subprocess.DEVNULL, timeout=3
            ).decode()
            for line in out.split("\n"):
                if "inet " in line:
                    info["ip"] = line.strip().split()[1]
        except Exception:
            pass
        # Link status
        try:
            out = subprocess.check_output(
                ["iw", "dev", iface, "link"], stderr=subprocess.DEVNULL, timeout=3
            ).decode()
            info["link"] = "Connected" if "Connected" in out else "Not connected"
        except Exception:
            info["link"] = "Unknown"
        result[iface] = info
    return result


def _get_service_status():
    """Check all known services."""
    result = {}
    for name, pidfile in SERVICE_PIDFILES.items():
        running, pid = _check_pid(pidfile)
        result[name] = {"running": running, "pid": pid}
    # Also check wpa_supplicant
    try:
        out = subprocess.check_output(["pgrep", "wpa_supplicant"], timeout=2).decode()
        pids = [int(p) for p in out.strip().split("\n") if p]
        result["wpa_supplicant"] = {"running": len(pids) > 0, "pids": pids}
    except Exception:
        result["wpa_supplicant"] = {"running": False, "pids": []}
    return result


def _get_internet():
    """Check internet connectivity."""
    try:
        subprocess.check_output(
            ["ping", "-c1", "-W2", "8.8.8.8"], stderr=subprocess.DEVNULL, timeout=3
        )
        return True
    except Exception:
        return False


def _get_memory():
    """Get memory info."""
    try:
        with open("/proc/meminfo") as f:
            mem = {}
            for line in f:
                if "MemTotal" in line:
                    mem["total_kb"] = int(line.split()[1])
                elif "MemFree" in line:
                    mem["free_kb"] = int(line.split()[1])
                elif "MemAvailable" in line:
                    mem["available_kb"] = int(line.split()[1])
            return mem
    except Exception:
        return {}


def _read_log(logfile, lines=50):
    """Read last N lines from a log file."""
    if not os.path.exists(logfile):
        return []
    try:
        with open(logfile) as f:
            all_lines = f.readlines()
        return [l.rstrip("\n") for l in all_lines[-lines:]]
    except Exception:
        return []


def register_sysmon_routes(app):
    """Register system monitor API routes on Flask app."""

    @app.route("/api/sysmon/status")
    def api_sysmon_status():
        """Full system status."""
        from flask import jsonify
        return jsonify({
            "code": 200,
            "data": {
                "wifi": _get_wifi_status(),
                "services": _get_service_status(),
                "internet": _get_internet(),
                "memory": _get_memory(),
            }
        })

    @app.route("/api/sysmon/log/<name>")
    def api_sysmon_log(name="sysmon"):
        """Read logs: sysmon, k230, book"""
        from flask import jsonify, request
        lines = request.args.get("lines", 50, type=int)
        log_map = {
            "sysmon": f"{LOG_DIR}/sysmon.log",
            "k230": "/tmp/k230_bridge.log",
        }
        filepath = log_map.get(name)
        if not filepath:
            return jsonify({"code": 400, "data": None, "message": f"Unknown log: {name}"})
        return jsonify({
            "code": 200,
            "data": {
                "name": name,
                "lines": _read_log(filepath, lines),
            }
        })

    @app.route("/api/sysmon/logs")
    def api_sysmon_logs():
        """List available log files."""
        from flask import jsonify
        logs = []
        for f in sorted(glob.glob(f"{LOG_DIR}/*.log")):
            size = os.path.getsize(f)
            logs.append({"name": os.path.basename(f), "size": size})
        for f in sorted(glob.glob("/tmp/*.log")):
            size = os.path.getsize(f)
            logs.append({"name": os.path.basename(f), "size": size, "volatile": True})
        return jsonify({"code": 200, "data": logs})
