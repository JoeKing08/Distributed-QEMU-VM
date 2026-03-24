#!/bin/bash
# Dump memory at BSP's current CS:IP from shared memory
# From strace: cs.base=0x60, rip=0xfdeb → linear 0x1004b

echo "=== BSP linear address 0x1004b - 32 bytes ==="
xxd -s 0x10040 -l 64 /dev/shm/wvm_fract_node0 2>/dev/null

# Also check what's at the SeaBIOS F-segment (0xF0000)
echo "=== BIOS entry area 0xFFFF0 (reset vector) ==="
xxd -s 0xFFFF0 -l 16 /dev/shm/wvm_fract_node0 2>/dev/null

# Dump more context around 0xFDEB in the F-segment (0xF0000+0xDEB)
echo "=== 0xF0000+0xDEB area ==="
xxd -s 0xF0DEB -l 32 /dev/shm/wvm_fract_node0 2>/dev/null

# SeaBIOS panic signature: search for CLI+HLT (0xFA 0xF4) near BSP area
echo "=== Search CLI+HLT near 0x10000 ==="
xxd -s 0x10000 -l 256 /dev/shm/wvm_fract_node0 2>/dev/null | grep -i "faf4" || echo "not found in 0x10000"

echo "=== Search CLI+HLT near 0xF0000 ==="
xxd -s 0xF0D00 -l 512 /dev/shm/wvm_fract_node0 2>/dev/null | grep -i "faf4" || echo "not found in 0xF0D00"

# Also check BDA (BIOS Data Area at 0x400) for POST status
echo "=== BDA at 0x400 ==="
xxd -s 0x400 -l 64 /dev/shm/wvm_fract_node0 2>/dev/null

# Also dump the debugcon more carefully
ART=$(cat /tmp/fract-kvm-artdir.txt 2>/dev/null)
echo "=== debugcon hexdump ==="
xxd "$ART/debugcon.log" 2>/dev/null | tail -10

echo "=== DONE ==="
