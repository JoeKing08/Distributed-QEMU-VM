#!/bin/bash
set -euo pipefail
ROOT=/workspaces/WaveVM_Frontier-X
ART_DIR="$ROOT/artifacts/tmp/fract-kvm-test-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$ART_DIR"
mount -o remount,size=8G /dev/shm 2>/dev/null
chmod 666 /dev/kvm 2>/dev/null

cat > "$ART_DIR/fract_2node.conf" << "EOF"
# NODE <physical_id> <ip> <sidecar_port> <cpu_capacity> <memory_gib> <dht_slots>
NODE 0 127.0.0.1 19120 2 2 1
NODE 1 127.0.0.1 19220 1 1 1
# VM <vm_id> <vcpus> <memory_mb> <compact|spread>
VM 0 3 3072 compact
EOF
cat > "$ART_DIR/sidecar_a_routes.txt" << "EOF"
ROUTE 0 1 127.0.0.1 19100
EOF
cat > "$ART_DIR/sidecar_b_routes.txt" << "EOF"
ROUTE 1 1 127.0.0.1 19200
EOF
cat > "$ART_DIR/l1a_routes.txt" << "EOF"
ROUTE 0 1 127.0.0.1 19120
EOF
cat > "$ART_DIR/l1b_routes.txt" << "EOF"
ROUTE 1 1 127.0.0.1 19220
EOF
cat > "$ART_DIR/l2_routes.txt" << "EOF"
ROUTE 0 1 127.0.0.1 19320
ROUTE 1 1 127.0.0.1 19420
EOF

rm -f /tmp/wvm_user_0.sock /tmp/wvm_user_1.sock /dev/shm/wvm_fract_*
sleep 1

QPATH="$ROOT/wavevm-qemu/build-native:$PATH"

make -C "$ROOT/ctl_tool" >/dev/null
"$ROOT/ctl_tool/wvm_ctl" --plan "$ART_DIR/fract_2node.conf" 0 \
  >"$ART_DIR/resource_plan.txt"

plan_value() {
  sed -n "s/^$1=//p" "$ART_DIR/resource_plan.txt" | head -n1
}

NODE0_CORES=$(plan_value WVM_PLAN_VM0_NODE0_VCPUS)
NODE1_CORES=$(plan_value WVM_PLAN_VM0_NODE1_VCPUS)
NODE0_MEM_MB=$(plan_value WVM_PLAN_VM0_NODE0_MEMORY_MB)
NODE1_MEM_MB=$(plan_value WVM_PLAN_VM0_NODE1_MEMORY_MB)
NODE0_VNODE=$(plan_value WVM_PLAN_NODE0_VNODE)
NODE1_VNODE=$(plan_value WVM_PLAN_NODE1_VNODE)
TOTAL_VCPUS=$(plan_value WVM_PLAN_VM0_VCPUS)
TOTAL_MEM_MB=$(plan_value WVM_PLAN_VM0_MEMORY_MB)

for value in NODE0_CORES NODE1_CORES NODE0_MEM_MB NODE1_MEM_MB NODE0_VNODE NODE1_VNODE TOTAL_VCPUS TOTAL_MEM_MB; do
  case "${!value}" in
    ''|*[!0-9]*)
      echo "ERROR: planner did not provide a numeric $value" >&2
      exit 1
      ;;
  esac
done
for value in NODE0_CORES NODE1_CORES NODE0_MEM_MB NODE1_MEM_MB TOTAL_VCPUS TOTAL_MEM_MB; do
  if [ "${!value}" -eq 0 ]; then
    echo "ERROR: planner did not provide a positive $value" >&2
    exit 1
  fi
done
if [ "$TOTAL_VCPUS" -ne $((NODE0_CORES + NODE1_CORES)) ] ||
   [ "$TOTAL_MEM_MB" -ne $((NODE0_MEM_MB + NODE1_MEM_MB)) ]; then
  echo "ERROR: planner output is inconsistent" >&2
  exit 1
fi

cpu_range() {
  if [ "$1" -eq "$2" ]; then
    printf '%s' "$1"
  else
    printf '%s-%s' "$1" "$2"
  fi
}

NODE0_CPU_RANGE=$(cpu_range 0 $((NODE0_CORES - 1)))
NODE1_CPU_RANGE=$(cpu_range "$NODE0_CORES" $((TOTAL_VCPUS - 1)))
WVM_NUMA_MAP="0:${NODE0_MEM_MB}:/wvm_fract_node0,100000000:${NODE1_MEM_MB}:/wvm_fract_node1"

# Gateways
env WVM_GATEWAY_SINGLE_RX=1 WVM_GATEWAY_DISABLE_REUSEPORT=1 WVM_GATEWAY_USE_RECVFROM=1 PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19520 127.0.0.1 19599 "$ART_DIR/l2_routes.txt" 19521 >"$ART_DIR/gw_l2.log" 2>&1 &
env WVM_GATEWAY_SINGLE_RX=1 WVM_GATEWAY_DISABLE_REUSEPORT=1 WVM_GATEWAY_USE_RECVFROM=1 PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19320 127.0.0.1 19520 "$ART_DIR/l1a_routes.txt" 19321 >"$ART_DIR/gw_l1a.log" 2>&1 &
env WVM_GATEWAY_SINGLE_RX=1 WVM_GATEWAY_DISABLE_REUSEPORT=1 WVM_GATEWAY_USE_RECVFROM=1 PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19420 127.0.0.1 19520 "$ART_DIR/l1b_routes.txt" 19421 >"$ART_DIR/gw_l1b.log" 2>&1 &
env WVM_GATEWAY_SINGLE_RX=1 WVM_GATEWAY_DISABLE_REUSEPORT=1 WVM_GATEWAY_USE_RECVFROM=1 PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19120 127.0.0.1 19320 "$ART_DIR/sidecar_a_routes.txt" 19121 >"$ART_DIR/gw_sidecar_a.log" 2>&1 &
env WVM_GATEWAY_SINGLE_RX=1 WVM_GATEWAY_DISABLE_REUSEPORT=1 WVM_GATEWAY_USE_RECVFROM=1 PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19220 127.0.0.1 19420 "$ART_DIR/sidecar_b_routes.txt" 19221 >"$ART_DIR/gw_sb.log" 2>&1 &

# Masters first
env WVM_NONBLOCK_RECV=1 WVM_POLL_TIMEOUT_MS=100 WVM_RX_THREAD_COUNT=1 WVM_DISABLE_REUSEPORT=1 PATH="$QPATH" WVM_INSTANCE_ID=0 WVM_SHM_FILE=/wvm_fract_node0 stdbuf -oL -eL "$ROOT/master_core/wavevm_node_master" "$NODE0_MEM_MB" 19100 "$ART_DIR/fract_2node.conf" 0 19121 19105 1 >"$ART_DIR/master0.log" 2>&1 &
env WVM_NONBLOCK_RECV=1 WVM_POLL_TIMEOUT_MS=100 WVM_RX_THREAD_COUNT=1 WVM_DISABLE_REUSEPORT=1 PATH="$QPATH" WVM_INSTANCE_ID=1 WVM_SHM_FILE=/wvm_fract_node1 stdbuf -oL -eL "$ROOT/master_core/wavevm_node_master" "$NODE1_MEM_MB" 19200 "$ART_DIR/fract_2node.conf" 1 19221 19205 1 >"$ART_DIR/master1.log" 2>&1 &
sleep 3

# Slaves
env WVM_NONBLOCK_RECV=1 PATH="$QPATH" \
  WVM_SHM_FILE=/wvm_fract_node0 WVM_NUMA_MAP="$WVM_NUMA_MAP" \
  stdbuf -oL -eL "$ROOT/slave_daemon/wavevm_node_slave" 19105 "$NODE0_CORES" "$NODE0_MEM_MB" "$NODE0_VNODE" 19121 >"$ART_DIR/slave0.log" 2>&1 &
env WVM_NONBLOCK_RECV=1 PATH="$QPATH" \
  WVM_SHM_FILE=/wvm_fract_node1 WVM_NUMA_MAP="$WVM_NUMA_MAP" \
  stdbuf -oL -eL "$ROOT/slave_daemon/wavevm_node_slave" 19205 "$NODE1_CORES" "$NODE1_MEM_MB" "$NODE1_VNODE" 19221 >"$ART_DIR/slave1.log" 2>&1 &
sleep 6

# QEMU
env WVM_NONBLOCK_RECV=1 WVM_POLL_TIMEOUT_MS=100 PATH="$QPATH" WVM_INSTANCE_ID=0 stdbuf -oL -eL "$ROOT/wavevm-qemu/build-native/qemu-system-x86_64" \
  -accel "wavevm,split=$NODE0_CORES" -machine q35 -m "$TOTAL_MEM_MB" -smp "$TOTAL_VCPUS" \
  -object memory-backend-file,id=ram0,size="${NODE0_MEM_MB}M",mem-path=/dev/shm/wvm_fract_node0,share=on \
  -object memory-backend-file,id=ram1,size="${NODE1_MEM_MB}M",mem-path=/dev/shm/wvm_fract_node1,share=on \
  -numa node,memdev=ram0,cpus="$NODE0_CPU_RANGE",nodeid=0 \
  -numa node,memdev=ram1,cpus="$NODE1_CPU_RANGE",nodeid=1 \
  -drive file="$ROOT/artifacts/images/cirros-0.6.2-x86_64-disk.img",if=virtio,format=qcow2,snapshot=on \
  -netdev user,id=ne,hostfwd=tcp::2226-:22 -device e1000,netdev=ne \
  -nographic -vga none \
  -monitor none >"$ART_DIR/vm.log" 2>&1 &
QPID=$!

echo "ART_DIR=$ART_DIR" > /tmp/kvm_test_info.txt

for i in $(seq 1 30); do
  sleep 60
  ALIVE="yes"; kill -0 $QPID 2>/dev/null || ALIVE="NO"
  echo "  ${i}m — Q=$ALIVE gw=$(pgrep -cf wavevm_gateway) master=$(pgrep -cf wavevm_node_master) slave=$(pgrep -cf wavevm_node_slave)"
done

echo "=== 30min summary ==="
echo "master0 timeouts: $(grep -c RPC [Tt]imeout "$ART_DIR/master0.log" 2>/dev/null || echo 0)"
echo "master1 timeouts: $(grep -c RPC [Tt]imeout "$ART_DIR/master1.log" 2>/dev/null || echo 0)"
echo "slave0 exits:"; grep -oP "exit=\d+" "$ART_DIR/slave0.log" 2>/dev/null | sort | uniq -c | sort -rn | head -5
echo "slave1 exits:"; grep -oP "exit=\d+" "$ART_DIR/slave1.log" 2>/dev/null | sort | uniq -c | sort -rn | head -5
echo "serial:"; tail -10 "$ART_DIR/vm-serial.log" 2>/dev/null || echo "(empty)"
wc -l "$ART_DIR"/*.log 2>/dev/null
