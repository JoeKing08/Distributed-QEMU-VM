#!/bin/bash
echo "=== dd from node0 offset 0 ==="
dd if=/dev/shm/wvm_fract_node0 bs=1 skip=0 count=16 2>/dev/null | xxd

echo "=== dd from node0 offset 0xe0000 (BIOS area) ==="
dd if=/dev/shm/wvm_fract_node0 bs=1 skip=$((0xe0000)) count=32 2>/dev/null | xxd

echo "=== dd from node0 offset 0xFFFF0 (reset vector) ==="
dd if=/dev/shm/wvm_fract_node0 bs=1 skip=$((0xFFFF0)) count=16 2>/dev/null | xxd

echo "=== dd from node0 offset 0x1004b (BSP current IP) ==="
dd if=/dev/shm/wvm_fract_node0 bs=1 skip=$((0x1004b)) count=32 2>/dev/null | xxd

echo "=== dd node0 at 0xFDEB0 ==="
dd if=/dev/shm/wvm_fract_node0 bs=1 skip=$((0xFDEB0)) count=64 2>/dev/null | xxd

echo "=== python3 mmap check ==="
python3 -c "
import mmap, os
fd = os.open('/dev/shm/wvm_fract_node0', os.O_RDONLY)
mm = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)
# Check offset 0
print('offset 0:', mm[0:16].hex())
# Check BIOS area
print('offset 0xe0000:', mm[0xe0000:0xe0010].hex())
# Check BSP IP area
print('offset 0x1004b:', mm[0x1004b:0x1006b].hex())
mm.close()
os.close(fd)
" 2>&1

echo "=== DONE ==="
