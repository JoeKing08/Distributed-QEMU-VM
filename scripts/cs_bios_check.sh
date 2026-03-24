#!/bin/bash
python3 -c "
import mmap, os, struct

fd = os.open('/dev/shm/wvm_fract_node0', os.O_RDONLY)
mm = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)

# Check E and F segments in detail
for base in [0xE0000, 0xE8000, 0xF0000, 0xF8000, 0xFC000, 0xFE000, 0xFF000, 0xFFF00]:
    data = mm[base:base+16]
    nz = sum(1 for b in mm[base:base+0x1000] if b != 0)
    print(f'  0x{base:05X}: {data.hex()} (nonzero bytes in page: {nz})')

# Specifically check 0xFFFF0 reset vector
rv = mm[0xFFFF0:0x100000]
print(f'  Reset vector 0xFFFF0: {rv.hex()}')

# Check what the JMP target should be
# EA XX XX YY YY = JMP FAR YY:XX
if rv[0] == 0xEA:
    off = struct.unpack('<H', rv[1:3])[0]
    seg = struct.unpack('<H', rv[3:5])[0]
    linear = seg * 16 + off
    print(f'  JMP FAR {seg:04X}:{off:04X} -> linear 0x{linear:05X}')
    target = mm[linear:linear+16]
    print(f'  Target code at 0x{linear:05X}: {target.hex()}')

# Check the actual BIOS file in QEMU build for comparison
import subprocess
bios = '/workspaces/WaveVM_Frontier-X/wavevm-qemu/build-native/pc-bios/bios-256k.bin'
if os.path.exists(bios):
    with open(bios, 'rb') as f:
        f.seek(-16, 2)  # Last 16 bytes = reset vector area
        bios_rv = f.read(16)
        print(f'  BIOS file reset vector: {bios_rv.hex()}')
        if bios_rv[0] == 0xEA:
            off = struct.unpack('<H', bios_rv[1:3])[0]
            seg = struct.unpack('<H', bios_rv[3:5])[0]
            print(f'  BIOS JMP FAR {seg:04X}:{off:04X}')
else:
    print(f'  BIOS file not found at {bios}')

# Also check wavevm_ram
fd2 = os.open('/dev/shm/wavevm_ram', os.O_RDONLY)
mm2 = mmap.mmap(fd2, 0, access=mmap.ACCESS_READ)
print(f'  wavevm_ram 0xFFFF0: {mm2[0xFFFF0:0x100000].hex()}')
mm2.close()
os.close(fd2)

mm.close()
os.close(fd)
" 2>&1

echo "=== DONE ==="
