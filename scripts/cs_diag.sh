#!/bin/bash
ART=$(cat /tmp/fract-kvm-artdir.txt 2>/dev/null)

# QEMU PID
QPID=$(pgrep -f qemu-system-x86_64 | head -1)
echo "=== QEMU PID=$QPID ==="

# CPU usage
ps -p $QPID -o %cpu,%mem,etime 2>/dev/null

# Thread states
echo "=== QEMU threads ==="
ps -T -p $QPID -o spid,pcpu,stat,wchan:30 2>/dev/null

# Check if BSP is stuck - look at more of vm.log
echo "=== vm.log BSP-DBG (all) ==="
grep "BSP-DBG" "$ART/vm.log" 2>/dev/null

# Full debugcon
echo "=== full debugcon ==="
cat "$ART/debugcon.log" 2>/dev/null

# Check master for any dispatch activity
echo "=== master0 dispatch ==="
grep -c "dispatch\|SEND\|vcpu\|RPC" "$ART/master0.log" 2>/dev/null || echo "0 matches"

# Gateway activity
echo "=== gw_l2 last 5 ==="
tail -5 "$ART/gw_l2.log" 2>/dev/null

echo "=== DONE ==="
