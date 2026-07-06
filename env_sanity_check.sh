#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="${ROOT_DIR}/env_sanity_${TS}.log"

# Ports for UDP loopback checks
PORT_A=19120
PORT_B=19220

log() {
  echo "$*" | tee -a "$LOG"
}

log "=== WaveVM Environment Sanity Check ==="
log "Timestamp: $TS"
log "Host: $(hostname)"
log "Kernel: $(uname -r)"
log "=== /dev/kvm ==="
if [ -e /dev/kvm ]; then
  ls -l /dev/kvm | tee -a "$LOG"
else
  log "MISSING: /dev/kvm"
fi

log "=== UDP loopback probe (A -> B) ==="
# Ensure clean state
rm -f /tmp/udp_probe_b.txt

# Start UDP listener on B
( timeout 3 bash -c "nc -u -l ${PORT_B} >/tmp/udp_probe_b.txt" ) >/dev/null 2>&1 &
LISTEN_PID=$!
sleep 0.2

# Send UDP probe from A
if command -v nc >/dev/null 2>&1; then
  echo "probe" | nc -u -w1 127.0.0.1 ${PORT_B} >/dev/null 2>&1 || true
else
  log "WARN: nc not found, skipping UDP probe"
fi

wait $LISTEN_PID 2>/dev/null || true

if [ -s /tmp/udp_probe_b.txt ]; then
  log "UDP probe: RECEIVED"
  log "UDP probe payload: $(cat /tmp/udp_probe_b.txt 2>/dev/null)"
else
  log "UDP probe: NOT RECEIVED"
fi

log "=== UDP sockets (ss) ==="
if command -v ss >/dev/null 2>&1; then
  ss -u -a -n -i | grep -E "${PORT_A}|${PORT_B}" | tee -a "$LOG" || true
else
  log "WARN: ss not found"
fi

log "=== Summary ==="
if [ -e /dev/kvm ]; then
  log "KVM: present"
else
  log "KVM: missing"
fi

if [ -s /tmp/udp_probe_b.txt ]; then
  log "UDP loopback: OK"
else
  log "UDP loopback: FAIL"
fi

log "Log saved to: $LOG"
