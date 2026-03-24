#!/bin/bash
ART=$(cat /tmp/fract-kvm-artdir.txt 2>/dev/null)

echo "=== master0 first 20 lines ==="
head -20 "$ART/master0.log" 2>/dev/null

echo "=== master1 first 20 lines ==="
head -20 "$ART/master1.log" 2>/dev/null

echo "=== slave0 count ==="
wc -l "$ART/slave0.log" 2>/dev/null

echo "=== slave1 count ==="
wc -l "$ART/slave1.log" 2>/dev/null

echo "=== gw sidecar_a first 5 ==="
head -5 "$ART/gw_sidecar_a.log" 2>/dev/null

echo "=== vm.log first 20 ==="
head -20 "$ART/vm.log" 2>/dev/null

# Check if QEMU actually connected to the master
echo "=== master0 user_backend lines ==="
grep -i "user\|connect\|accept\|socket\|bind" "$ART/master0.log" 2>/dev/null | head -10

echo "=== python3 more memory checks ==="
python3 -c "
import mmap, os
fd = os.open('/dev/shm/wvm_fract_node0', os.O_RDONLY)
mm = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)
# Reset vector: 0xFFFF0 should have a JMP to BIOS entry
print('reset_vector 0xFFFF0:', mm[0xFFFF0:0x100000].hex())
# BIOS code at 0xF0000
print('bios_entry 0xF0000:', mm[0xF0000:0xF0010].hex())
# Check if there's any non-zero data in the E segment
nonzero = False
for i in range(0xe0000, 0xf0000, 4096):
    if mm[i:i+4096] != b'\x00' * 4096:
        nonzero = True
        print(f'nonzero at 0x{i:x}')
        break
if not nonzero:
    print('E0000-F0000: all zeros')
# Check F segment
nonzero_f = False
for i in range(0xf0000, 0x100000, 4096):
    if mm[i:i+4096] != b'\x00' * 4096:
        nonzero_f = True
        print(f'F seg nonzero at 0x{i:x}')
        break
if not nonzero_f:
    print('F0000-100000: all zeros')
mm.close()
os.close(fd)
" 2>&1

echo "=== DONE ==="
