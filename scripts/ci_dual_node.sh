#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-$PWD}"
RESULTS="${RESULTS:-/tmp/wavevm-test-results}"
ART_DIR="$RESULTS/dual-node-$(date +%Y%m%d-%H%M%S)"
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
export WVM_NONBLOCK_RECV="${WVM_NONBLOCK_RECV:-1}"
export WVM_POLL_TIMEOUT_MS="${WVM_POLL_TIMEOUT_MS:-100}"
export WVM_RX_THREAD_COUNT="${WVM_RX_THREAD_COUNT:-1}"
export WVM_DISABLE_REUSEPORT="${WVM_DISABLE_REUSEPORT:-1}"

log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

cleanup() {
  set +e
  log "cleanup"
  pkill -f qemu-system-x86_64 2>/dev/null || true
  pkill -f wavevm_node_master 2>/dev/null || true
  pkill -f wavevm_node_slave 2>/dev/null || true
  pkill -f wavevm_gateway 2>/dev/null || true
  rm -f /tmp/wvm_user_0.sock /tmp/wvm_user_1.sock 2>/dev/null || true
}
trap cleanup EXIT

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
  log "tailscale up role=$ROLE host=$THIS_HOST ip=$TAIL_IP peer=$PEER_HOST"
}

wait_peer_ip() {
  local ip=""
  for i in $(seq 1 180); do
    ip=$(sudo tailscale --socket=/tmp/tailscaled.sock ip -4 "$PEER_HOST" 2>/dev/null | head -n1 || true)
    if [ -n "$ip" ]; then
      echo "$ip"
      return 0
    fi
    if [ $((i % 10)) -eq 0 ]; then
      log "waiting for peer $PEER_HOST in tailnet (${i}s)"
    fi
    sleep 1
  done
  echo "ERROR: peer $PEER_HOST did not appear in tailnet" >&2
  return 1
}

wait_tcp() {
  local host="$1" port="$2" name="$3" max="${4:-180}"
  for i in $(seq 1 "$max"); do
    if timeout 2 bash -c "</dev/tcp/$host/$port" >/dev/null 2>&1; then
      log "$name ready at $host:$port"
      return 0
    fi
    if [ $((i % 15)) -eq 0 ]; then
      log "waiting for $name at $host:$port (${i}s)"
    fi
    sleep 1
  done
  echo "ERROR: timeout waiting for $name at $host:$port" >&2
  return 1
}

write_common_config() {
  CFG="$ART_DIR/fract_2node.conf"
  cat >"$CFG" <<'EOCFG'
NODE 0 127.0.0.1 19120 1 1
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

  wait_tcp "$node_b_ip" 19220 "node-b sidecar" 180
  wait_tcp "$node_b_ip" 19200 "node-b master" 180
  sleep 8

  log "start QEMU forced TCG"
  env WVM_INSTANCE_ID=0 WVM_DISABLE_AUTO_KVM=1 stdbuf -oL -eL \
    "$ROOT/wavevm-qemu/build-native/qemu-system-x86_64" \
    -accel wavevm -machine q35 -m 3072 -smp 3 \
    -object memory-backend-ram,id=ram0,size=2048M \
    -object memory-backend-ram,id=ram1,size=1024M \
    -numa node,memdev=ram0,cpus=0-1,nodeid=0 \
    -numa node,memdev=ram1,cpus=2,nodeid=1 \
    -drive file="$ROOT/artifacts/images/cirros-0.6.2-x86_64-disk.img",if=virtio,format=qcow2,snapshot=on \
    -netdev user,id=ne,hostfwd=tcp::2226-:22 -device e1000,netdev=ne \
    -display none -vga none \
    -serial file:"$ART_DIR/vm-serial.log" -monitor none \
    >"$ART_DIR/vm.log" 2>&1 &
  QPID=$!

  for i in $(seq 1 10); do
    sleep 60
    if kill -0 "$QPID" 2>/dev/null; then q_alive=yes; else q_alive=NO; fi
    m0_to=$(grep -ci 'RPC Timeout' "$ART_DIR/master0.log" 2>/dev/null || true)
    log "${i}m elapsed Q=$q_alive master0_timeouts=$m0_to"
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
  for i in $(seq 1 15); do
    sleep 60
    summarize_light
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

summarize() {
  log "summary ART_DIR=$ART_DIR"
  summarize_light
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
  chmod 666 /dev/kvm 2>/dev/null || true
  rm -f /tmp/wvm_user_0.sock /tmp/wvm_user_1.sock /dev/shm/wvm_fract_* 2>/dev/null || true

  start_tailscale
  PEER_IP=$(wait_peer_ip)
  echo "$PEER_IP" >"$ART_DIR/peer_ip.txt"
  log "peer ip $PEER_IP"

  if [ "$ROLE" = "A" ]; then
    start_node_a "$PEER_IP"
  else
    start_node_b "$PEER_IP"
  fi
}

main "$@"
