#!/bin/bash
set -x
cd /workspaces/WaveVM_Frontier-X

# Rebuild slave
echo "=== Building slave ==="
cd slave_daemon && make -j$(nproc) 2>&1
echo "slave exit: $?"
cd ..

# Rebuild master
echo "=== Building master ==="
cd master_core && gcc -O2 -I../common_include -pthread -o wavevm_node_master main_wrapper.c logic_core.c user_backend.c 2>&1
echo "master exit: $?"
cd ..

# Check gateway exists
ls -la gateway_service/wavevm_gateway 2>&1

# Run test in background
echo "=== Starting SMP test ==="
mount -o remount,size=8G /dev/shm 2>/dev/null || true
bash artifacts/fract-kvm-test/run_smp_numa.sh &
sleep 15

# Check it started
ART=$(cat /tmp/fract-kvm-artdir.txt 2>/dev/null)
echo "ART=$ART"
ps aux | grep -E 'qemu|wavevm' | grep -v grep | head -20
echo "=== debugcon so far ==="
cat "$ART/debugcon.log" 2>/dev/null | tail -30
echo "=== serial bytes ==="
wc -c "$ART/vm-serial.log" 2>/dev/null
echo "=== DONE initial check ==="
