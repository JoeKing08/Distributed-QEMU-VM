#!/bin/bash
set -x
cd /workspaces/WaveVM_Frontier-X

# Kill ALL old processes
killall -9 qemu-system-x86_64 wavevm_gateway wavevm_node_master wavevm_node_slave 2>/dev/null
sleep 2
# Verify killed
ps aux | grep -E 'qemu|wavevm' | grep -v grep && echo "WARNING: processes still alive" || echo "All killed"

# Clean stale sockets
rm -f /tmp/wvm_user_0.sock /tmp/wvm_user_1.sock 2>/dev/null

# Run test fresh
echo "=== Starting SMP test ==="
mount -o remount,size=8G /dev/shm 2>/dev/null || true
bash artifacts/fract-kvm-test/run_smp_numa.sh &
sleep 20

ART=$(cat /tmp/fract-kvm-artdir.txt 2>/dev/null)
echo "ART=$ART"

echo "=== processes ==="
ps aux | grep -E 'qemu|wavevm' | grep -v grep | wc -l
echo "=== debugcon ==="
wc -l "$ART/debugcon.log" 2>/dev/null
tail -30 "$ART/debugcon.log" 2>/dev/null
echo "=== serial ==="
wc -c "$ART/vm-serial.log" 2>/dev/null
echo "=== vm.log ==="
tail -10 "$ART/vm.log" 2>/dev/null
echo "=== DONE ==="
