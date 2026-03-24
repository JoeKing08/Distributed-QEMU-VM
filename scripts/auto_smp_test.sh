#!/bin/bash
# Auto SMP test - runs on codespace start, pushes results back
set -x
LOG="/tmp/auto_smp_test.log"
exec > "$LOG" 2>&1

cd /workspaces/WaveVM_Frontier-X || exit 1
git pull origin main || true

# Check if previous test was running (processes survive restart? no - they don't)
# Kill any stale processes
killall -9 qemu-system-x86_64 wavevm_gateway wavevm_node_master wavevm_node_slave 2>/dev/null
sleep 2

# Rebuild binaries
echo "=== Rebuilding slave ==="
cd slave_daemon && make -j$(nproc) 2>&1 && cd ..
echo "=== Rebuilding master ==="
cd master_core && gcc -O2 -I../common_include -pthread -o wavevm_node_master main_wrapper.c logic_core.c user_backend.c 2>&1 && cd ..

# Run the SMP test
echo "=== Starting SMP NUMA test ==="
bash artifacts/fract-kvm-test/run_smp_numa.sh &
TEST_PID=$!

# Wait for boot (up to 5 minutes)
ART=""
for i in $(seq 1 30); do
    sleep 10
    if [ -f /tmp/fract-kvm-artdir.txt ]; then
        ART=$(cat /tmp/fract-kvm-artdir.txt)
        if [ -f "$ART/vm-serial.log" ]; then
            SERIAL_SZ=$(wc -c < "$ART/vm-serial.log")
            echo "[${i}0s] serial=$SERIAL_SZ bytes"
            if [ "$SERIAL_SZ" -gt 100 ]; then
                echo "=== Serial output detected, boot progressing ==="
                break
            fi
        fi
        if [ -f "$ART/debugcon.log" ]; then
            echo "[${i}0s] debugcon tail:"
            tail -5 "$ART/debugcon.log"
        fi
    fi
done

# Collect results
echo "=== Collecting results ==="
RESULT_DIR="artifacts/auto-test-results"
mkdir -p "$RESULT_DIR"
TIMESTAMP=$(date +%Y%m%d-%H%M%S)

if [ -n "$ART" ]; then
    cp "$ART/debugcon.log" "$RESULT_DIR/debugcon-$TIMESTAMP.log" 2>/dev/null
    cp "$ART/vm-serial.log" "$RESULT_DIR/serial-$TIMESTAMP.log" 2>/dev/null
    tail -50 "$ART/slave0.log" > "$RESULT_DIR/slave0-tail-$TIMESTAMP.log" 2>/dev/null
    tail -50 "$ART/slave1.log" > "$RESULT_DIR/slave1-tail-$TIMESTAMP.log" 2>/dev/null
    tail -20 "$ART/master0.log" > "$RESULT_DIR/master0-tail-$TIMESTAMP.log" 2>/dev/null
    tail -20 "$ART/master1.log" > "$RESULT_DIR/master1-tail-$TIMESTAMP.log" 2>/dev/null
    tail -20 "$ART/vm.log" > "$RESULT_DIR/vm-tail-$TIMESTAMP.log" 2>/dev/null
    # Summary
    echo "debugcon lines: $(wc -l < "$ART/debugcon.log" 2>/dev/null || echo 0)" > "$RESULT_DIR/summary-$TIMESTAMP.txt"
    echo "serial bytes: $(wc -c < "$ART/vm-serial.log" 2>/dev/null || echo 0)" >> "$RESULT_DIR/summary-$TIMESTAMP.txt"
    echo "qemu running: $(pgrep -c qemu-system 2>/dev/null || echo 0)" >> "$RESULT_DIR/summary-$TIMESTAMP.txt"
    echo "timestamp: $TIMESTAMP" >> "$RESULT_DIR/summary-$TIMESTAMP.txt"
    echo "debugcon last 10 lines:" >> "$RESULT_DIR/summary-$TIMESTAMP.txt"
    tail -10 "$ART/debugcon.log" >> "$RESULT_DIR/summary-$TIMESTAMP.txt" 2>/dev/null
fi

cp "$LOG" "$RESULT_DIR/auto-run-$TIMESTAMP.log" 2>/dev/null

# Push results
cd /workspaces/WaveVM_Frontier-X
git add artifacts/auto-test-results/
git commit -m "auto: SMP test results $TIMESTAMP" 2>/dev/null
git push origin main 2>/dev/null

echo "=== DONE ==="
