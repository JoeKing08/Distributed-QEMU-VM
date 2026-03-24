#!/bin/bash
echo "=== whoami ==="
whoami
echo "=== artdir ==="
cat /tmp/fract-kvm-artdir.txt 2>/dev/null || echo "no artdir"
echo "=== processes ==="
ps aux | grep -E 'qemu|wavevm' | grep -v grep || echo "no wavevm/qemu processes"
echo "=== /dev/kvm ==="
ls -la /dev/kvm 2>/dev/null || echo "no /dev/kvm"
echo "=== DONE ==="
