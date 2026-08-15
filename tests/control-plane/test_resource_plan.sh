#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
ctl="$repo_root/ctl_tool/wvm_ctl"
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/wavevm-resource-plan.XXXXXX")

cleanup() {
    rm -rf "$tmpdir"
}
trap cleanup EXIT

fail() {
    printf 'resource-plan test: %s\n' "$*" >&2
    exit 1
}

expect_contains() {
    local output=$1
    local expected=$2

    grep -Fqx "$expected" <<<"$output" ||
        fail "missing expected output: $expected"
}

expect_rejected() {
    local config=$1
    local expected=$2
    local output

    if output=$("$ctl" --plan "$config" 2>&1); then
        fail "expected planner rejection for $config"
    fi
    grep -Fq "$expected" <<<"$output" ||
        fail "unexpected rejection for $config: $output"
}

make -C "$repo_root/ctl_tool" >/dev/null

cat >"$tmpdir/compact.conf" <<'EOF'
NODE 0 127.0.0.1 19100 2 3 1
NODE 2 127.0.0.1 19200 2 3 1
VM 7 3 3072 compact
EOF
compact_output=$("$ctl" --plan "$tmpdir/compact.conf")
expect_contains "$compact_output" "WVM_PLAN_VM7_HOST_NODE=0"
expect_contains "$compact_output" "WVM_PLAN_VM7_NODE0_VCPUS=2"
expect_contains "$compact_output" "WVM_PLAN_VM7_NODE2_VCPUS=1"
expect_contains "$compact_output" "WVM_PLAN_VM7_NODE0_MEMORY_MB=3072"
[[ $(grep -Fxc "WVM_PLAN_VM7_HOST_NODE=0" <<<"$compact_output") -eq 1 ]] ||
    fail "host node is emitted more than once"

cat >"$tmpdir/spread.conf" <<'EOF'
NODE 0 127.0.0.1 19100 2 4 1
NODE 2 127.0.0.1 19200 2 4 1
VM 8 4 4096 spread
EOF
spread_output=$("$ctl" --plan "$tmpdir/spread.conf")
expect_contains "$spread_output" "WVM_PLAN_VM8_HOST_NODE=0"
expect_contains "$spread_output" "WVM_PLAN_VM8_NODE0_VCPUS=2"
expect_contains "$spread_output" "WVM_PLAN_VM8_NODE2_VCPUS=2"
expect_contains "$spread_output" "WVM_PLAN_VM8_NODE0_MEMORY_MB=2048"
expect_contains "$spread_output" "WVM_PLAN_VM8_NODE2_MEMORY_MB=2048"

cat >"$tmpdir/cpu-overcommit.conf" <<'EOF'
NODE 0 127.0.0.1 19100 1 1 1
NODE 1 127.0.0.1 19200 1 1 1
VM 1 3 512 compact
EOF
expect_rejected "$tmpdir/cpu-overcommit.conf" "cluster CPU capacity is exhausted"

cat >"$tmpdir/memory-overcommit.conf" <<'EOF'
NODE 0 127.0.0.1 19100 1 1 1
VM 1 1 2048 compact
EOF
expect_rejected "$tmpdir/memory-overcommit.conf" "cluster memory capacity is exhausted"

printf 'resource-plan tests: PASS\n'
