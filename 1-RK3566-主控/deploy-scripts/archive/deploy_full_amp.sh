#!/bin/bash
# Full deployment script - runs after board is online
# Usage: bash deploy_full.sh
set -e
BOARD="YOUR_BOARD_IP"
PASS="${SSH_PASS:-YOUR_SSH_PASSWORD}"

echo "=== 1. Verify board ==="
sshpass -p "$PASS" ssh root@$BOARD "echo OK; uptime; dmesg | grep 'Linux version'"

echo "=== 2. Check clean boot (no OOPS) ==="
sshpass -p "$PASS" ssh root@$BOARD "dmesg | grep -i 'Oops\|panic\|Internal error' || echo 'No OOPS found - clean!'"

echo "=== 3. Deploy __va psci_safe.ko ==="
sshpass -p "$PASS" scp psci_safe_va_v2.ko root@$BOARD:/opt/amp/psci_safe.ko

echo "=== 4. Load module ==="
sshpass -p "$PASS" ssh root@$BOARD "insmod /opt/amp/psci_safe.ko fw_path=/opt/amp/rtthread_pdm.bin; echo ret=\$?"

echo "=== 5. Check dmesg for PSCI results ==="
sshpass -p "$PASS" ssh root@$BOARD "dmesg | grep -i psci | tail -15"

echo "=== 6. Verify RT-Thread (IPC magic should be 0x5254434B) ==="
sshpass -p "$PASS" ssh root@$BOARD "devmem 0x4840010C 32; devmem 0x48418008 32; devmem 0x48000010 32; devmem 0x48000030 32"

echo "=== 7. Check PDM audio (wait 3s for samples) ==="
sleep 3
sshpass -p "$PASS" ssh root@$BOARD "devmem 0x48418008 32"

echo "=== DONE ==="
