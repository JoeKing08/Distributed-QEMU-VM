#!/usr/bin/env bash
set -euo pipefail
ROOT=/workspaces/WaveVM_Frontier-X
ART_DIR="$ROOT/artifacts/tmp/fract-tcg-test-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$ART_DIR"

limit_log() {
  local out="$1"
  local max="${WVM_TEST_LOG_LIMIT_BYTES:-134217728}"
  awk -v out="$out" -v max="$max" '
    {
      line = $0 ORS
      n = length(line)
      if (bytes < max) {
        remain = max - bytes
        if (n <= remain) {
          printf "%s", line >> out
          bytes += n
        } else {
          printf "%s", substr(line, 1, remain) >> out
          bytes = max
        }
        fflush(out)
      } else if (!warned) {
        printf "\n[WaveVM-Test] log limit reached (%d bytes); further output discarded\n", max >> out
        fflush(out)
        warned = 1
      }
    }
  '
}

mount -o remount,size=8G /dev/shm

# === NODE config (for masters — g_gateways[] points to local sidecars) ===
CFG="$ART_DIR/fract_2node.conf"
cat > "$CFG" <<'EOCFG'
# NODE <physical_id> <ip> <sidecar_port> <cpu_capacity> <memory_gib> <dht_slots>
NODE 0 127.0.0.1 19120 2 2 1
NODE 1 127.0.0.1 19220 1 1 1
# VM <vm_id> <vcpus> <memory_mb> <compact|spread>
VM 0 3 3072 compact
EOCFG

# === 3-level fractal gateway route configs ===

# Sidecar A: only knows local node 0 → master0
cat > "$ART_DIR/sidecar_a_routes.txt" <<'EOCFG'
ROUTE 0 1 127.0.0.1 19100
EOCFG

# Sidecar B: only knows local node 1 → master1
cat > "$ART_DIR/sidecar_b_routes.txt" <<'EOCFG'
ROUTE 1 1 127.0.0.1 19200
EOCFG

# L1a: knows node 0 → sidecar_a
cat > "$ART_DIR/l1a_routes.txt" <<'EOCFG'
ROUTE 0 1 127.0.0.1 19120
EOCFG

# L1b: knows node 1 → sidecar_b
cat > "$ART_DIR/l1b_routes.txt" <<'EOCFG'
ROUTE 1 1 127.0.0.1 19220
EOCFG

# L2 (top): full table → L1a, L1b
cat > "$ART_DIR/l2_routes.txt" <<'EOCFG'
ROUTE 0 1 127.0.0.1 19320
ROUTE 1 1 127.0.0.1 19420
EOCFG

# === Kill old processes ===
pkill -f "wavevm_node_master" 2>/dev/null || true
pkill -f "wavevm_node_slave" 2>/dev/null || true
pkill -f "wavevm_gateway" 2>/dev/null || true
pkill -f "qemu-system-x86_64" 2>/dev/null || true
rm -f /tmp/wvm_user_0.sock /tmp/wvm_user_1.sock 2>/dev/null || true
sleep 1

mv /dev/kvm /dev/kvm.off 2>/dev/null || true
trap 'kill ${LOG_MON:-} 2>/dev/null || true; mv /dev/kvm.off /dev/kvm 2>/dev/null || true; pkill -f wavevm_node_master 2>/dev/null || true; pkill -f wavevm_node_slave 2>/dev/null || true; pkill -f wavevm_gateway 2>/dev/null || true; pkill -f qemu-system-x86_64 2>/dev/null || true' EXIT

monitor_log_sizes() {
  local max="${WVM_TEST_LOG_LIMIT_BYTES:-134217728}"
  local f size

  while true; do
    for f in "$ART_DIR"/*.log; do
      [ -e "$f" ] || continue
      size=$(stat -c '%s' "$f" 2>/dev/null || echo 0)
      if [ "${size:-0}" -gt "$max" ]; then
        echo "ERROR: log limit exceeded: $f ${size} > ${max}" >&2
        kill "$$" 2>/dev/null || true
        return 124
      fi
    done
    sleep 2
  done
}

monitor_log_sizes &
LOG_MON=$!

wait_for_ssh_login() {
  for _ in $(seq 1 40); do
    if timeout 10s sshpass -p "gocubsgo" ssh \
      -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null \
      -o LogLevel=ERROR \
      -o ConnectTimeout=5 \
      -p 2226 cirros@127.0.0.1 'echo ok' >/dev/null 2>&1; then
      echo "=== guest SSH login ready ==="
      return 0
    fi
    sleep 3
  done
  echo "ERROR: guest SSH login did not become ready" >&2
  return 1
}

run_guest_cpu_smoke() {
  local smoke_log="$ART_DIR/guest_cpu_smoke.txt"
  echo "=== guest CPU smoke: start 3-way busy loop ==="
  timeout 30s sshpass -p "gocubsgo" ssh \
    -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR \
    -o ConnectTimeout=5 \
    -p 2226 cirros@127.0.0.1 \
    'sh -c "yes >/dev/null & yes >/dev/null & yes >/dev/null & sleep 12"' \
    >"$smoke_log" 2>&1
  cat "$smoke_log" || true
}

wait_for_remote_tcg() {
  local i rem sent ack
  for i in $(seq 1 24); do
    rem=$(grep -c 'WVM-REMOTE.*cpu=2' "$ART_DIR/vm.log" 2>/dev/null || true)
    sent=$(grep -c 'IPC VCPU_RUN] vcpu=2 ' "$ART_DIR/master0.log" 2>/dev/null || true)
    ack=$(grep -c 'IPC VCPU_RUN RET] vcpu=2.*target=1 rpc_ret=0 ack_status=0 ack_mode=1' "$ART_DIR/master0.log" 2>/dev/null || true)
    rem="${rem:-0}"
    sent="${sent:-0}"
    ack="${ack:-0}"
    echo "=== remote smoke poll ${i}: remote=$rem sent=$sent returned=$ack ==="
    if [ "$rem" -gt 0 ] && [ "$sent" -gt 0 ] && [ "$ack" -gt 0 ]; then
      return 0
    fi
    sleep 5
  done
  echo "ERROR: remote TCG evidence not observed" >&2
  return 1
}

QPATH="$ROOT/wavevm-qemu/build-native:$PATH"

make -C "$ROOT/ctl_tool" >/dev/null
"$ROOT/ctl_tool/wvm_ctl" --plan "$CFG" 0 >"$ART_DIR/resource_plan.txt"

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

# === Start 5 gateways (top-down: L2 → L1a/L1b → sidecar_a/sidecar_b) ===

echo "=== Starting L2 gateway (top, full table) ==="
# L2: listen 19520, upstream 19599 (dummy/none), ctrl 19521
(env PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19520 127.0.0.1 19599 "$ART_DIR/l2_routes.txt" 19521) >"$ART_DIR/gw_l2.log" 2>&1 &
GL2=$!

echo "=== Starting L1 gateways ==="
# L1a: listen 19320, upstream L2:19520, ctrl 19321
(env PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19320 127.0.0.1 19520 "$ART_DIR/l1a_routes.txt" 19321) >"$ART_DIR/gw_l1a.log" 2>&1 &
GL1A=$!
# L1b: listen 19420, upstream L2:19520, ctrl 19421
(env PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19420 127.0.0.1 19520 "$ART_DIR/l1b_routes.txt" 19421) >"$ART_DIR/gw_l1b.log" 2>&1 &
GL1B=$!

echo "=== Starting sidecar gateways ==="
# Sidecar A: listen 19120, upstream L1a:19320, ctrl 19121
(env PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19120 127.0.0.1 19320 "$ART_DIR/sidecar_a_routes.txt" 19121) >"$ART_DIR/gw_sidecar_a.log" 2>&1 &
GSA=$!
# Sidecar B: listen 19220, upstream L1b:19420, ctrl 19221
(env PATH="$QPATH" stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" 19220 127.0.0.1 19420 "$ART_DIR/sidecar_b_routes.txt" 19221) >"$ART_DIR/gw_sidecar_b.log" 2>&1 &
GSB=$!

echo "=== Starting slaves ==="
(env PATH="$QPATH" WVM_TCG_QEMU_MACHINE=q35 WVM_TCG_QEMU_MEM_MB="$TOTAL_MEM_MB" WVM_SHM_FILE=/wvm_fract_node0 stdbuf -oL -eL "$ROOT/slave_daemon/wavevm_node_slave" 19105 "$NODE0_CORES" "$NODE0_MEM_MB" "$NODE0_VNODE" 19121) >"$ART_DIR/slave0.log" 2>&1 &
S0=$!
(env PATH="$QPATH" WVM_TCG_QEMU_MACHINE=q35 WVM_TCG_QEMU_MEM_MB="$TOTAL_MEM_MB" WVM_SHM_FILE=/wvm_fract_node1 stdbuf -oL -eL "$ROOT/slave_daemon/wavevm_node_slave" 19205 "$NODE1_CORES" "$NODE1_MEM_MB" "$NODE1_VNODE" 19221) >"$ART_DIR/slave1.log" 2>&1 &
S1=$!

echo "=== Starting masters ==="
(env PATH="$QPATH" WVM_INSTANCE_ID=0 WVM_SHM_FILE=/wvm_fract_node0 stdbuf -oL -eL "$ROOT/master_core/wavevm_node_master" "$NODE0_MEM_MB" 19100 "$CFG" 0 19121 19105 1) >"$ART_DIR/master0.log" 2>&1 &
M0=$!
(env PATH="$QPATH" WVM_INSTANCE_ID=1 WVM_SHM_FILE=/wvm_fract_node1 stdbuf -oL -eL "$ROOT/master_core/wavevm_node_master" "$NODE1_MEM_MB" 19200 "$CFG" 1 19221 19205 1) >"$ART_DIR/master1.log" 2>&1 &
M1=$!

echo "=== Waiting 8s for convergence ==="
sleep 8

echo "=== Starting QEMU (TCG mode, no KVM) ==="
(env PATH="$QPATH" WVM_INSTANCE_ID=0 WVM_SHM_FILE=/wvm_fract_node0 stdbuf -oL -eL "$ROOT/wavevm-qemu/build-native/qemu-system-x86_64" \
  -accel "wavevm,split=$NODE0_CORES" -machine q35 -m "$TOTAL_MEM_MB" -smp "$TOTAL_VCPUS" \
  -object memory-backend-ram,id=ram0,size="${NODE0_MEM_MB}M" \
  -object memory-backend-ram,id=ram1,size="${NODE1_MEM_MB}M" \
  -numa node,memdev=ram0,cpus="$NODE0_CPU_RANGE",nodeid=0 \
  -numa node,memdev=ram1,cpus="$NODE1_CPU_RANGE",nodeid=1 \
  -drive file="$ROOT/artifacts/images/cirros-0.6.2-x86_64-disk.img",if=virtio,format=qcow2 \
  -netdev user,id=ne,hostfwd=tcp::2226-:22 -device e1000,netdev=ne \
  -display none -vga none \
  -serial file:"$ART_DIR/vm-serial.log" -monitor none) >"$ART_DIR/vm.log" 2>&1 &
Q=$!

echo "=== Waiting 1200s (20 min) for QEMU TCG boot ==="
for i in $(seq 1 20); do
  sleep 60
  echo "  ${i}m elapsed — Q alive: $(kill -0 $Q 2>/dev/null && echo yes || echo NO)"
done

echo ""
echo "=== Checking processes ==="
for p in GL2 GL1A GL1B GSA GSB S0 S1 M0 M1 Q; do
  pid=${!p}
  if kill -0 $pid 2>/dev/null; then
    echo "  $p ($pid): alive"
  else
    echo "  $p ($pid): DEAD"
  fi
done

echo ""
echo "=== Checking port 2226 ==="
ss -tln | grep 2226 || echo "  Port 2226 not listening"

echo ""
echo "=== vCPU distribution ==="
echo "master0:" && grep -oP "vcpu=\d+" "$ART_DIR/master0.log" | sort | uniq -c || true
echo "vm.log:" && grep -oP "vcpu=\d+" "$ART_DIR/vm.log" | sort | uniq -c || true

echo ""
echo "=== RPC timeout count ==="
echo "master0: $(grep -c 'RPC timeout' "$ART_DIR/master0.log" 2>/dev/null || echo 0)"
echo "master1: $(grep -c 'RPC timeout' "$ART_DIR/master1.log" 2>/dev/null || echo 0)"

echo ""
echo "=== SSH banner check ==="
timeout 5 bash -c 'echo "" | nc 127.0.0.1 2226' 2>/dev/null || echo "  (no banner / timeout)"

wait_for_ssh_login
run_guest_cpu_smoke
wait_for_remote_tcg

echo ""
echo "=== Checking for crashes ==="
if grep -rEi "Segmentation fault|Bus error" "$ART_DIR"/*.log 2>/dev/null; then
  echo "CRASHES DETECTED"
else
  echo "No crashes found"
fi

echo ""
echo "=== Log tails ==="
for f in gw_l2 gw_l1a gw_l1b gw_sidecar_a gw_sidecar_b slave0 slave1 master0 master1 vm; do
  echo "--- $f.log (last 15 lines) ---"
  tail -15 "$ART_DIR/$f.log" 2>/dev/null || echo "  (empty)"
done

echo ""
echo "=== VM serial log (last 50 lines) ==="
tail -50 "$ART_DIR/vm-serial.log" 2>/dev/null || echo "  (empty)"

echo ""
echo "=== Log sizes ==="
wc -l "$ART_DIR"/*.log 2>/dev/null

echo ""
echo "=== ART_DIR: $ART_DIR ==="
