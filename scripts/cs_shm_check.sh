#!/bin/bash
echo "=== /dev/shm files ==="
ls -la /dev/shm/wvm_* 2>/dev/null || echo "no wvm files"
ls -la /dev/shm/ 2>/dev/null | head -20

echo "=== check QEMU RAM file ==="
stat /dev/shm/wvm_fract_node0 2>/dev/null || echo "node0 not found"
stat /dev/shm/wvm_fract_node1 2>/dev/null || echo "node1 not found"

echo "=== first 16 bytes of node0 ==="
xxd -l 16 /dev/shm/wvm_fract_node0 2>/dev/null || echo "cannot read node0"

echo "=== BIOS ROM region check (e0000-fffff in node0) ==="
xxd -s 0xe0000 -l 16 /dev/shm/wvm_fract_node0 2>/dev/null || echo "cannot read"

echo "=== Try reading from proc maps ==="
QPID=$(pgrep -f qemu-system-x86_64 | head -1)
cat /proc/$QPID/maps 2>/dev/null | grep -i "shm\|wvm\|ram" | head -10

echo "=== DONE ==="
