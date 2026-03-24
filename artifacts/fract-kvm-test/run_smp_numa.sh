#!/bin/bash
# SMP NUMA KVM Fractal Test Script
# Tests 3-CPU SMP boot with 2 NUMA nodes via WaveVM distributed vCPU dispatch.
# Prerequisites:
#   - KVM available (/dev/kvm)
#   - QEMU built at wavevm-qemu/build-native/
#   - Slave/Master/Gateway binaries compiled
#   - CirrosOS image at artifacts/images/cirros-0.6.2-x86_64-disk.img

cd /workspaces/WaveVM_Frontier-X
ROOT=/workspaces/WaveVM_Frontier-X
ART_DIR="$ROOT/artifacts/tmp/fract-kvm-debug2-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$ART_DIR"
mount -o remount,size=8G /dev/shm 2>/dev/null || true

CFG="$ART_DIR/fract_2node.conf"
printf "NODE 0 127.0.0.1 19120 1 1\nNODE 1 127.0.0.1 19220 1 1\n" > "$CFG"
printf "ROUTE 0 1 127.0.0.1 19100\n" > "$ART_DIR/sidecar_a_routes.txt"
printf "ROUTE 1 1 127.0.0.1 19200\n" > "$ART_DIR/sidecar_b_routes.txt"
printf "ROUTE 0 1 127.0.0.1 19120\n" > "$ART_DIR/l1a_routes.txt"
printf "ROUTE 1 1 127.0.0.1 19220\n" > "$ART_DIR/l1b_routes.txt"
printf "ROUTE 0 1 127.0.0.1 19320\nROUTE 1 1 127.0.0.1 19420\n" > "$ART_DIR/l2_routes.txt"
rm -f /tmp/wvm_user_0.sock /tmp/wvm_user_1.sock 2>/dev/null

export PATH="$ROOT/wavevm-qemu/build-native:$PATH"
export WVM_GATEWAY_SINGLE_RX=1 WVM_GATEWAY_DISABLE_REUSEPORT=1 WVM_GATEWAY_USE_RECVFROM=1 WVM_GATEWAY_MULTI_QUEUE=0
GW="$ROOT/gateway_service/wavevm_gateway"
MS="$ROOT/master_core/wavevm_node_master"
SL="$ROOT/slave_daemon/wavevm_node_slave"

# Both slaves map full guest physical address space via WVM_NUMA_MAP
# Format: "gpa_hex:size_mb:shm_path,..."
NUMA_MAP="0:2048:/wvm_fract_node0,100000000:1024:/wvm_fract_node1"

# Gateways (L2 root + L1 per-node + sidecar per-node)
WVM_GATEWAY_DISABLE_LEARN_ROUTE=1 $GW 19520 127.0.0.1 19599 "$ART_DIR/l2_routes.txt" 19521 >"$ART_DIR/gw_l2.log" 2>&1 &
WVM_GATEWAY_DISABLE_LEARN_ROUTE=1 $GW 19320 127.0.0.1 19520 "$ART_DIR/l1a_routes.txt" 19321 >"$ART_DIR/gw_l1a.log" 2>&1 &
WVM_GATEWAY_DISABLE_LEARN_ROUTE=1 $GW 19420 127.0.0.1 19520 "$ART_DIR/l1b_routes.txt" 19421 >"$ART_DIR/gw_l1b.log" 2>&1 &
$GW 19120 127.0.0.1 19320 "$ART_DIR/sidecar_a_routes.txt" 19121 >"$ART_DIR/gw_sidecar_a.log" 2>&1 &
$GW 19220 127.0.0.1 19420 "$ART_DIR/sidecar_b_routes.txt" 19221 >"$ART_DIR/gw_sidecar_b.log" 2>&1 &

# Slaves (each maps ALL NUMA nodes so full GPA space is accessible)
export WVM_NONBLOCK_RECV=1
WVM_NUMA_MAP="$NUMA_MAP" $SL 19105 2 3072 0 19121 >"$ART_DIR/slave0.log" 2>&1 &
WVM_NUMA_MAP="$NUMA_MAP" $SL 19205 1 3072 1 19221 >"$ART_DIR/slave1.log" 2>&1 &

# Masters
export WVM_POLL_TIMEOUT_MS=100 WVM_RX_THREAD_COUNT=1 WVM_DISABLE_REUSEPORT=1
WVM_INSTANCE_ID=0 WVM_SHM_FILE=/wvm_fract_node0 $MS 2048 19100 "$CFG" 0 19121 19105 1 >"$ART_DIR/master0.log" 2>&1 &
WVM_INSTANCE_ID=1 WVM_SHM_FILE=/wvm_fract_node1 $MS 1024 19200 "$CFG" 1 19221 19205 1 >"$ART_DIR/master1.log" 2>&1 &

sleep 8

# QEMU (BSP runs locally, APs dispatched to slaves via WaveVM)
WVM_INSTANCE_ID=0 $ROOT/wavevm-qemu/build-native/qemu-system-x86_64 \
  -accel wavevm -machine q35,smm=off -m 3072 -smp 3 \
  -object memory-backend-file,id=ram0,size=2048M,mem-path=/dev/shm/wvm_fract_node0,share=on \
  -object memory-backend-file,id=ram1,size=1024M,mem-path=/dev/shm/wvm_fract_node1,share=on \
  -numa node,memdev=ram0,cpus=0-1,nodeid=0 \
  -numa node,memdev=ram1,cpus=2,nodeid=1 \
  -drive file="$ROOT/artifacts/images/cirros-0.6.2-x86_64-disk.img",if=virtio,format=qcow2,snapshot=on \
  -netdev user,id=ne,hostfwd=tcp::2226-:22 -device e1000,netdev=ne \
  -display none -vga none \
  -debugcon file:"$ART_DIR/debugcon.log" -global isa-debugcon.iobase=0x402 \
  -serial file:"$ART_DIR/vm-serial.log" -monitor none >"$ART_DIR/vm.log" 2>&1 &

echo "$ART_DIR" > /tmp/fract-kvm-artdir.txt
echo "ALL STARTED ART=$ART_DIR"
wait
