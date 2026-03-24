#!/bin/bash
ART=$(cat /tmp/fract-kvm-artdir.txt 2>/dev/null)
echo "ART=$ART"
echo "=== debugcon lines ==="
wc -l "$ART/debugcon.log" 2>/dev/null
echo "=== debugcon last 40 ==="
tail -40 "$ART/debugcon.log" 2>/dev/null
echo "=== serial bytes ==="
wc -c "$ART/vm-serial.log" 2>/dev/null
echo "=== serial head ==="
head -20 "$ART/vm-serial.log" 2>/dev/null
echo "=== slave0 tail ==="
tail -5 "$ART/slave0.log" 2>/dev/null
echo "=== slave1 tail ==="
tail -5 "$ART/slave1.log" 2>/dev/null
echo "=== DONE ==="
