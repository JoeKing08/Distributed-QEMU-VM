#!/bin/bash
ART=$(cat /tmp/fract-kvm-artdir.txt 2>/dev/null)
echo "=== master0 last 30 ==="
tail -30 "$ART/master0.log" 2>/dev/null
echo "=== master1 last 30 ==="
tail -30 "$ART/master1.log" 2>/dev/null
echo "=== slave0 last 30 ==="
tail -30 "$ART/slave0.log" 2>/dev/null
echo "=== slave1 last 30 ==="
tail -30 "$ART/slave1.log" 2>/dev/null
echo "=== vm.log last 30 ==="
tail -30 "$ART/vm.log" 2>/dev/null
echo "=== qemu proc ==="
ps aux | grep qemu | grep -v grep
echo "=== DONE ==="
