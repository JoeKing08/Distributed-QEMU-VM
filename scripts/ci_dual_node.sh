#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-$PWD}"
RESULTS="${RESULTS:-/tmp/wavevm-test-results}"
ACCEL="${WAVEVM_CI_ACCEL:-tcg}"
case "$ACCEL" in
  tcg|kvm) ;;
  *) echo "ERROR: WAVEVM_CI_ACCEL must be tcg or kvm, got '$ACCEL'" >&2; exit 2 ;;
esac

ART_DIR="$RESULTS/dual-node-$ACCEL-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$ART_DIR"

PROJECT_USER="${CIRCLE_PROJECT_USERNAME:-${WAVEVM_CI_ROLE:-}}"
case "$PROJECT_USER" in
  jktest020|020|node-a|a) ROLE="A" ;;
  jktest021|021|node-b|b) ROLE="B" ;;
  *)
    echo "ERROR: cannot infer CI role from CIRCLE_PROJECT_USERNAME='$PROJECT_USER'" >&2
    echo "Set WAVEVM_CI_ROLE=node-a or node-b to run manually." >&2
    exit 2
    ;;
esac

if [ -z "${TS_AUTHKEY:-}" ]; then
  echo "ERROR: TS_AUTHKEY is not set in CircleCI environment" >&2
  exit 2
fi

NODE_A_HOST="${NODE_A_HOST:-wavevm-ci-a}"
NODE_B_HOST="${NODE_B_HOST:-wavevm-ci-b}"
THIS_HOST="$NODE_A_HOST"
PEER_HOST="$NODE_B_HOST"
if [ "$ROLE" = "B" ]; then
  THIS_HOST="$NODE_B_HOST"
  PEER_HOST="$NODE_A_HOST"
fi

QPATH="$ROOT/wavevm-qemu/build-native:$ROOT/wavevm-qemu/build:$PATH"
export PATH="$QPATH"
export WVM_GATEWAY_SINGLE_RX="${WVM_GATEWAY_SINGLE_RX:-1}"
export WVM_GATEWAY_DISABLE_REUSEPORT="${WVM_GATEWAY_DISABLE_REUSEPORT:-1}"
export WVM_GATEWAY_USE_RECVFROM="${WVM_GATEWAY_USE_RECVFROM:-1}"
export WVM_GATEWAY_DISABLE_LEARN_ROUTE="${WVM_GATEWAY_DISABLE_LEARN_ROUTE:-1}"
export WVM_NONBLOCK_RECV="${WVM_NONBLOCK_RECV:-1}"
export WVM_POLL_TIMEOUT_MS="${WVM_POLL_TIMEOUT_MS:-100}"
export WVM_RX_THREAD_COUNT="${WVM_RX_THREAD_COUNT:-1}"
export WVM_DISABLE_REUSEPORT="${WVM_DISABLE_REUSEPORT:-1}"

log() { printf '[%s] [%s/%s] %s\n' "$(date +%H:%M:%S)" "$ACCEL" "$ROLE" "$*"; }
HEARTBEAT_PID=""

cleanup() {
  set +e
  log "cleanup"
  if [ -n "$HEARTBEAT_PID" ]; then
    kill "$HEARTBEAT_PID" 2>/dev/null || true
  fi
  pkill -f qemu-system-x86_64 2>/dev/null || true
  pkill -f wavevm_node_master 2>/dev/null || true
  pkill -f wavevm_node_slave 2>/dev/null || true
  pkill -f wavevm_gateway 2>/dev/null || true
  sudo rmmod wavevm 2>/dev/null || true
  if [ -e /dev/kvm.off ] && [ ! -e /dev/kvm ]; then
    sudo mv /dev/kvm.off /dev/kvm 2>/dev/null || true
  fi
  rm -f /tmp/wvm_user_0.sock /tmp/wvm_user_1.sock 2>/dev/null || true
}
trap cleanup EXIT

start_ci_heartbeat() {
  (
    while true; do
      sleep 60
      log "ci heartbeat"
      summarize_light || true
    done
  ) &
  HEARTBEAT_PID=$!
}

install_tailscale() {
  if command -v tailscale >/dev/null 2>&1 && command -v tailscaled >/dev/null 2>&1; then
    return
  fi
  log "installing tailscale"
  curl -fsSL https://tailscale.com/install.sh | sh
}

start_tailscale() {
  install_tailscale
  sudo pkill tailscaled 2>/dev/null || true
  sudo rm -f /tmp/tailscaled.sock /tmp/tailscaled.state 2>/dev/null || true
  sudo tailscaled --state=/tmp/tailscaled.state --socket=/tmp/tailscaled.sock >"$ART_DIR/tailscaled.log" 2>&1 &
  for _ in $(seq 1 30); do
    if sudo tailscale --socket=/tmp/tailscaled.sock status >/dev/null 2>&1; then
      break
    fi
    sleep 1
  done
  sudo tailscale --socket=/tmp/tailscaled.sock up \
    --authkey "$TS_AUTHKEY" \
    --hostname "$THIS_HOST" \
    --accept-routes=false \
    --ssh=false
  TAIL_IP=$(sudo tailscale --socket=/tmp/tailscaled.sock ip -4 | head -n1)
  if [ -z "$TAIL_IP" ]; then
    echo "ERROR: failed to get tailscale IPv4" >&2
    exit 1
  fi
  echo "$TAIL_IP" >"$ART_DIR/tailscale_ip.txt"
  log "tailscale up host=$THIS_HOST ip=$TAIL_IP peer=$PEER_HOST"
}

peer_candidates() {
  local host="$1"
  local status_file="$ART_DIR/tailscale_status.json"
  sudo tailscale --socket=/tmp/tailscaled.sock status --json >"$status_file" 2>/dev/null || true
  python3 - "$status_file" "$host" <<'PY' || true
import json
import sys

path, target = sys.argv[1], sys.argv[2]
try:
    data = json.load(open(path, encoding="utf-8"))
except Exception:
    sys.exit(0)

matches = []
for peer in (data.get("Peer") or {}).values():
    host = peer.get("HostName") or ""
    dns = (peer.get("DNSName") or "").rstrip(".")
    names = {host, dns, dns.split(".", 1)[0] if dns else ""}
    if target not in names:
        continue
    ips = peer.get("TailscaleIPs") or []
    if not ips:
        continue
    matches.append((not bool(peer.get("Online")), ips[0]))

seen = set()
for _, ip in sorted(matches):
    if ip and ip not in seen:
        print(ip)
        seen.add(ip)
PY
  sudo tailscale --socket=/tmp/tailscaled.sock ip -4 "$host" 2>/dev/null | head -n5 || true
}

wait_peer_ip() {
  local ip=""
  for i in $(seq 1 180); do
    ip=$(peer_candidates "$PEER_HOST" | head -n1 || true)
    if [ -n "$ip" ]; then
      echo "$ip"
      return 0
    fi
    if [ $((i % 10)) -eq 0 ]; then
      log "waiting for peer $PEER_HOST in tailnet (${i}s)" >&2
    fi
    sleep 1
  done
  echo "ERROR: peer $PEER_HOST did not appear in tailnet" >&2
  return 1
}

wait_peer_services() {
  local host="$1" max="${2:-240}" ip="" candidates=""
  for i in $(seq 1 "$max"); do
    candidates=$(peer_candidates "$host" | awk 'NF && !seen[$0]++')
    for ip in $candidates; do
      if sudo tailscale --socket=/tmp/tailscaled.sock ping -c 1 "$ip" >/dev/null 2>&1; then
        log "peer $host selected at $ip" >&2
        echo "$ip"
        return 0
      fi
    done
    if [ $((i % 15)) -eq 0 ]; then
      log "waiting for $host services; candidates=$(printf '%s' "$candidates" | tr '\n' ',' | sed 's/,$//') (${i}s)" >&2
      sudo tailscale --socket=/tmp/tailscaled.sock status | sed -n '1,12p' >&2 || true
    fi
    sleep 1
  done
  echo "ERROR: peer $host services did not become reachable" >&2
  sudo tailscale --socket=/tmp/tailscaled.sock status >&2 || true
  return 1
}

write_common_config() {
  CFG="$ART_DIR/fract_2node.conf"
  cat >"$CFG" <<'EOCFG'
NODE 0 127.0.0.1 19120 2 1
NODE 1 127.0.0.1 19220 1 1
EOCFG
}

start_gateway() {
  local name="$1" listen="$2" upstream_ip="$3" upstream_port="$4" routes="$5" ctrl="$6"
  log "start gateway $name listen=$listen upstream=$upstream_ip:$upstream_port ctrl=$ctrl"
  env stdbuf -oL -eL "$ROOT/gateway_service/wavevm_gateway" \
    "$listen" "$upstream_ip" "$upstream_port" "$routes" "$ctrl" \
    >"$ART_DIR/$name.log" 2>&1 &
}

prepare_mode() {
  if [ "$ACCEL" = "tcg" ]; then
    log "TCG Mode B/B: do not load wavevm.ko; temporarily hide /dev/kvm for pure TCG"
    sudo rmmod wavevm 2>/dev/null || true
    if [ -e /dev/kvm ] && [ ! -e /dev/kvm.off ]; then
      sudo mv /dev/kvm /dev/kvm.off 2>/dev/null || true
    fi
    return
  fi

  if [ "$ROLE" = "B" ]; then
    log "KVM mixed mode node B: keep slave/master in Mode B; do not load wavevm.ko"
    sudo rmmod wavevm 2>/dev/null || true
    return
  fi

  log "KVM mixed mode node A: build/load wavevm.ko for Mode A"
  if [ ! -e /dev/kvm ]; then
    echo "ERROR: /dev/kvm missing on node A; cannot run KVM mixed-mode test" >&2
    exit 1
  fi
  sudo chmod 666 /dev/kvm 2>/dev/null || true
  sudo rmmod wavevm 2>/dev/null || true
  make -C "/lib/modules/$(uname -r)/build" M="$ROOT/master_core" modules
  sudo insmod "$ROOT/master_core/wavevm.ko"
  for _ in $(seq 1 20); do
    [ -e /dev/wavevm ] && break
    sleep 1
  done
  if [ ! -e /dev/wavevm ]; then
    echo "ERROR: /dev/wavevm did not appear after insmod" >&2
    exit 1
  fi
  sudo chmod 666 /dev/wavevm /dev/kvm 2>/dev/null || true
  ls -la /dev/wavevm /dev/kvm | tee "$ART_DIR/kvm_devices.txt"
}

qemu_memory_args() {
  if [ "$ACCEL" = "kvm" ]; then
    cat <<'EOF_ARGS'
-object
memory-backend-file,id=ram0,size=2048M,mem-path=/dev/shm/wvm_fract_node0,share=on
-object
memory-backend-file,id=ram1,size=1024M,mem-path=/dev/shm/wvm_fract_node1,share=on
EOF_ARGS
  else
    cat <<'EOF_ARGS'
-object
memory-backend-ram,id=ram0,size=2048M
-object
memory-backend-ram,id=ram1,size=1024M
EOF_ARGS
  fi
}

start_node_a() {
  local node_b_ip="$1"
  write_common_config
  cat >"$ART_DIR/sidecar_a_routes.txt" <<'EOF_A_SC'
ROUTE 0 1 127.0.0.1 19100
EOF_A_SC
  cat >"$ART_DIR/l1a_routes.txt" <<'EOF_A_L1'
ROUTE 0 1 127.0.0.1 19120
EOF_A_L1
  cat >"$ART_DIR/l2_routes.txt" <<EOF_A_L2
ROUTE 0 1 127.0.0.1 19320
ROUTE 1 1 $node_b_ip 19420
EOF_A_L2

  start_gateway gw_l2 19520 127.0.0.1 19599 "$ART_DIR/l2_routes.txt" 19521
  start_gateway gw_l1a 19320 127.0.0.1 19520 "$ART_DIR/l1a_routes.txt" 19321
  start_gateway gw_sidecar_a 19120 127.0.0.1 19320 "$ART_DIR/sidecar_a_routes.txt" 19121

  log "start slave0/master0"
  env WVM_SHM_FILE=/wvm_fract_node0 stdbuf -oL -eL \
    "$ROOT/slave_daemon/wavevm_node_slave" 19105 2 2048 0 19121 \
    >"$ART_DIR/slave0.log" 2>&1 &
  env WVM_INSTANCE_ID=0 WVM_SHM_FILE=/wvm_fract_node0 stdbuf -oL -eL \
    "$ROOT/master_core/wavevm_node_master" 2048 19100 "$CFG" 0 19121 19105 1 \
    >"$ART_DIR/master0.log" 2>&1 &

  log "node-b tailscale peer selected at $node_b_ip; WaveVM ports are UDP, so QEMU/RPC logs are the readiness check"
  sleep 8

  local -a mem_args
  mapfile -t mem_args < <(qemu_memory_args)
  log "start QEMU accel=$ACCEL"
  env WVM_INSTANCE_ID=0 WVM_SHM_FILE=/wvm_fract_node0 stdbuf -oL -eL \
    "$ROOT/wavevm-qemu/build-native/qemu-system-x86_64" \
    -accel wavevm -machine q35 -m 3072 -smp 3 \
    "${mem_args[@]}" \
    -numa node,memdev=ram0,cpus=0-1,nodeid=0 \
    -numa node,memdev=ram1,cpus=2,nodeid=1 \
    -drive file="$ROOT/artifacts/images/cirros-0.6.2-x86_64-disk.img",if=virtio,format=qcow2,snapshot=on \
    -netdev user,id=ne,hostfwd=tcp::2226-:22 -device e1000,netdev=ne \
    -display none -vga none \
    -serial file:"$ART_DIR/vm-serial.log" -monitor none \
    >"$ART_DIR/vm.log" 2>&1 &
  QPID=$!

  for i in $(seq 1 30); do
    sleep 60
    if kill -0 "$QPID" 2>/dev/null; then q_alive=yes; else q_alive=NO; fi
    m0_to=$(timeout 5 grep -ci 'RPC Timeout' "$ART_DIR/master0.log" 2>/dev/null || true)
    m1_to=$(timeout 5 grep -ci 'RPC Timeout' "$ART_DIR/master1.log" 2>/dev/null || true)
    m0_to="${m0_to:-0}"
    m1_to="${m1_to:-0}"
    log "${i}m elapsed Q=$q_alive master0_timeouts=$m0_to master1_timeouts=$m1_to"
    if [ -f "$ART_DIR/vm-serial.log" ] && grep -q 'cirros login:' "$ART_DIR/vm-serial.log"; then
      log "guest reached cirros login"
      break
    fi
  done

  log "port 2226 check"
  (ss -tln 2>/dev/null | grep 2226) | tee "$ART_DIR/ss_2226.txt" || true
  log "ssh banner check"
  timeout 8 bash -c 'cat < /dev/tcp/127.0.0.1/2226' >"$ART_DIR/ssh_banner.txt" 2>&1 || true
  head -c 120 "$ART_DIR/ssh_banner.txt" || true
  printf '\n'

  summarize
  if ! kill -0 "$QPID" 2>/dev/null; then
    echo "ERROR: QEMU died" >&2
    return 1
  fi
  if ! grep -q 'cirros login:' "$ART_DIR/vm-serial.log" 2>/dev/null; then
    echo "ERROR: guest did not reach cirros login" >&2
    return 1
  fi
  if grep -qi 'RPC Timeout' "$ART_DIR/master0.log" "$ART_DIR/master1.log" 2>/dev/null; then
    echo "ERROR: RPC timeout detected" >&2
    return 1
  fi
  if ! grep -q 'SSH-' "$ART_DIR/ssh_banner.txt" 2>/dev/null; then
    echo "ERROR: SSH banner missing" >&2
    return 1
  fi
  wait_for_ssh_login
  run_guest_cpu_smoke
  wait_for_remote_tcg
}

start_node_b() {
  local node_a_ip="$1"
  write_common_config
  cat >"$ART_DIR/sidecar_b_routes.txt" <<'EOF_B_SC'
ROUTE 1 1 127.0.0.1 19200
EOF_B_SC
  cat >"$ART_DIR/l1b_routes.txt" <<'EOF_B_L1'
ROUTE 1 1 127.0.0.1 19220
EOF_B_L1

  start_gateway gw_l1b 19420 "$node_a_ip" 19520 "$ART_DIR/l1b_routes.txt" 19421
  start_gateway gw_sidecar_b 19220 127.0.0.1 19420 "$ART_DIR/sidecar_b_routes.txt" 19221

  log "start slave1/master1"
  env WVM_SHM_FILE=/wvm_fract_node1 stdbuf -oL -eL \
    "$ROOT/slave_daemon/wavevm_node_slave" 19205 1 1024 1 19221 \
    >"$ART_DIR/slave1.log" 2>&1 &
  env WVM_INSTANCE_ID=1 WVM_SHM_FILE=/wvm_fract_node1 stdbuf -oL -eL \
    "$ROOT/master_core/wavevm_node_master" 1024 19200 "$CFG" 1 19221 19205 1 \
    >"$ART_DIR/master1.log" 2>&1 &

  log "node B ready; holding for node A test"
  ss -tlnp 2>/dev/null | grep -E '19220|19200|19205|19420|19421|19221' || true
  for i in $(seq 1 20); do
    sleep 60
    summarize_light
    ss -tlnp 2>/dev/null | grep -E '19220|19200|19205|19420|19421|19221' || true
    log "node B hold ${i}m"
  done
}

summarize_light() {
  printf 'processes: gw=%s master=%s slave=%s qemu=%s\n' \
    "$(pgrep -cf wavevm_gateway || true)" \
    "$(pgrep -cf wavevm_node_master || true)" \
    "$(pgrep -cf wavevm_node_slave || true)" \
    "$(pgrep -cf qemu-system-x86_64 || true)"
}

wait_for_ssh_login() {
  local user="${1:-cirros}"
  local pass="${2:-gocubsgo}"
  for _ in $(seq 1 40); do
    if timeout 10s sshpass -p "$pass" ssh \
      -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null \
      -o LogLevel=ERROR \
      -o ConnectTimeout=5 \
      -p 2226 "$user@127.0.0.1" 'echo ok' >/dev/null 2>&1; then
      log "guest SSH login ready"
      return 0
    fi
    sleep 3
  done
  echo "ERROR: guest SSH login did not become ready" >&2
  return 1
}

run_guest_cpu_smoke() {
  local smoke_log="$ART_DIR/guest_cpu_smoke.txt"
  log "guest CPU smoke: start 3-way busy loop"
  timeout 30s sshpass -p "gocubsgo" ssh \
    -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR \
    -o ConnectTimeout=5 \
    -p 2226 cirros@127.0.0.1 \
    'sh -c "yes >/dev/null & yes >/dev/null & yes >/dev/null & sleep 12"' \
    >"$smoke_log" 2>&1
  local rc=$?
  cat "$smoke_log" || true
  return "$rc"
}

wait_for_remote_tcg() {
  local i rem req ack
  for i in $(seq 1 24); do
    rem=$(grep -c 'WVM-REMOTE.*cpu=2' "$ART_DIR/vm.log" 2>/dev/null || true)
    req=$(grep -c 'guest_vcpu=2' "$ART_DIR/slave0.log" 2>/dev/null || true)
    ack=$(grep -c 'TCG ack' "$ART_DIR/slave0.log" 2>/dev/null || true)
    rem="${rem:-0}"
    req="${req:-0}"
    ack="${ack:-0}"
    log "remote smoke poll ${i}: remote=$rem guest_vcpu2=$req ack=$ack"
    if [ "$rem" -gt 0 ] && [ "$req" -gt 0 ] && [ "$ack" -gt 0 ]; then
      return 0
    fi
    sleep 5
  done
  echo "ERROR: remote TCG evidence not observed" >&2
  return 1
}

summarize_tcg_signals() {
  printf 'WVM-REMOTE(cpu2)=%s\n' "$(grep -c 'WVM-REMOTE.*cpu=2' "$ART_DIR/vm.log" 2>/dev/null || true)"
  printf 'guest_vcpu=2=%s\n' "$(grep -c 'guest_vcpu=2' "$ART_DIR/slave0.log" 2>/dev/null || true)"
  printf 'TCG ack=%s\n' "$(grep -c 'TCG ack' "$ART_DIR/slave0.log" 2>/dev/null || true)"
}

summarize() {
  log "summary ART_DIR=$ART_DIR"
  summarize_light
  summarize_tcg_signals
  for f in gw_l2 gw_l1a gw_l1b gw_sidecar_a gw_sidecar_b slave0 slave1 master0 master1 vm; do
    if [ -f "$ART_DIR/$f.log" ]; then
      echo "--- $f.log tail ---"
      tail -20 "$ART_DIR/$f.log" || true
    fi
  done
  echo "--- serial tail ---"
  tail -60 "$ART_DIR/vm-serial.log" 2>/dev/null || true
  echo "--- timeout counts ---"
  grep -Rci 'RPC Timeout' "$ART_DIR"/*.log 2>/dev/null || true
  echo "--- log sizes ---"
  wc -l "$ART_DIR"/*.log 2>/dev/null || true
}

main() {
  cd "$ROOT"
  cleanup
  mount -o remount,size=8G /dev/shm 2>/dev/null || true
  rm -f /tmp/wvm_user_0.sock /tmp/wvm_user_1.sock /dev/shm/wvm_fract_* 2>/dev/null || true
  log "ART_DIR=$ART_DIR"

  prepare_mode
  start_ci_heartbeat
  start_tailscale
  if [ "$ROLE" = "A" ]; then
    PEER_IP=$(wait_peer_services "$PEER_HOST") || exit 1
  else
    PEER_IP=$(wait_peer_ip) || exit 1
  fi
  echo "$PEER_IP" >"$ART_DIR/peer_ip.txt"
  log "peer ip $PEER_IP"

  if [ "$ROLE" = "A" ]; then
    start_node_a "$PEER_IP"
  else
    start_node_b "$PEER_IP"
  fi
}

main "$@"
