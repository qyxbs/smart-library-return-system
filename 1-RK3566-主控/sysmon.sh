#!/bin/sh
# sysmon.sh — System monitor with persistent logging (2026-06-27)
# Runs as daemon, logs to /userdata/log/ (persistent across reboots)

LOG_DIR=/userdata/log
LOG_FILE=$LOG_DIR/sysmon.log
MAX_LOG_SIZE=1048576  # 1MB rotation

mkdir -p $LOG_DIR

# Rotate if too large
if [ -f "$LOG_FILE" ] && [ $(stat -c%s "$LOG_FILE" 2>/dev/null || echo 0) -gt $MAX_LOG_SIZE ]; then
    mv $LOG_FILE $LOG_FILE.old
fi

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a $LOG_FILE
}

# Check and log key services
check_service() {
    local name=$1 pidfile=$2 procname=$3
    if [ -n "$pidfile" ] && [ -f "$pidfile" ]; then
        pid=$(cat $pidfile)
        if kill -0 $pid 2>/dev/null; then
            return 0
        fi
    fi
    if pgrep "$procname" > /dev/null 2>&1; then
        return 0
    fi
    return 1
}

# Monitor loop
INTERVAL=30
log "=== System monitor started (interval=${INTERVAL}s) ==="

while true; do
    # WiFi
    WIFI_SSID=""
    WIFI_IP=""
    for iface in wlan0 wlan1; do
        [ -d /sys/class/net/$iface ] || continue
        SSID=$(iw dev $iface info 2>/dev/null | grep ssid | awk '{print $2}')
        IP=$(ip -4 addr show $iface 2>/dev/null | grep inet | awk '{print $2}')
        [ -n "$SSID" ] && WIFI_SSID="$WIFI_SSID $iface=$SSID"
        [ -n "$IP" ] && WIFI_IP="$WIFI_IP $iface=$IP"
    done

    # Services status
    BOOK_OK=$(check_service "book_app" "/var/run/book_app.pid" "python.*app" && echo 1 || echo 0)
    K230_OK=$(check_service "k230" "/var/run/k230_bridge.pid" "k230_bridge" && echo 1 || echo 0)
    LLAMA_OK=$(pgrep "llama-server" > /dev/null 2>&1 && echo 1 || echo 0)
    VOICE_OK=$(pgrep "voice_web_app" > /dev/null 2>&1 && echo 1 || echo 0)
    AP_OK=$(pgrep "hostapd" > /dev/null 2>&1 && echo 1 || echo 0)

    # Internet check
    INTERNET=0
    ping -c1 -W2 8.8.8.8 > /dev/null 2>&1 && INTERNET=1

    # Memory
    MEM_FREE=$(free | awk '/Mem:/ {print $4}')
    MEM_TOTAL=$(free | awk '/Mem:/ {print $2}')

    # Log status line
    log "STATUS wifi=[$WIFI_SSID] ip=[$WIFI_IP] net=$INTERNET book=$BOOK_OK k230=$K230_OK llama=$LLAMA_OK voice=$VOICE_OK ap=$AP_OK mem=${MEM_FREE}/${MEM_TOTAL}kB"

    # Alert if anything is down
    [ "$BOOK_OK" = "0" ] && log "ALERT: book_app DOWN"
    [ "$K230_OK" = "0" ] && log "ALERT: k230_bridge DOWN"
    [ "$INTERNET" = "0" ] && log "ALERT: No internet"

    sleep $INTERVAL
done
