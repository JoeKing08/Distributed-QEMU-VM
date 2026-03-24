#!/bin/bash
ART=$(cat /tmp/fract-kvm-artdir.txt 2>/dev/null)

# All BSP-DBG and IO lines from vm.log
echo "=== vm.log KVM-IO lines ==="
grep "KVM-IO" "$ART/vm.log" 2>/dev/null | tail -20

# All WaveVM related lines
echo "=== vm.log WaveVM lines ==="
grep -i "wavevm\|WVM\|region\|BSP" "$ART/vm.log" 2>/dev/null | tail -40

# Check /proc/QPID/stack for BSP thread
QPID=$(pgrep -f qemu-system-x86_64 | head -1)
echo "=== BSP thread stack (kvm_vcpu_block thread) ==="
for tid in $(ls /proc/$QPID/task/); do
    wchan=$(cat /proc/$QPID/task/$tid/wchan 2>/dev/null)
    if [ "$wchan" = "kvm_vcpu_block" ]; then
        echo "TID=$tid wchan=$wchan"
        cat /proc/$QPID/task/$tid/stack 2>/dev/null
        break
    fi
done

# Check fw_cfg port access: port 0x510 (selector) and 0x511 (data)
echo "=== strace QEMU 1s ==="
timeout 2 strace -p $QPID -e trace=ioctl -f 2>&1 | head -40

echo "=== DONE ==="
