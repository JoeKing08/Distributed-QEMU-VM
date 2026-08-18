#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/wavevm-executor-bridge.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

gcc -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L \
    -I"$repo_root/common_include" -I"$repo_root/node_runtime" \
    "$repo_root/common_include/wavevm_sha256.c" \
    "$repo_root/common_include/wavevm_canonical.c" \
    "$repo_root/common_include/wavevm_identity.c" \
    "$repo_root/common_include/wavevm_manifest.c" \
    "$repo_root/common_include/wavevm_lifecycle.c" \
    "$repo_root/common_include/wavevm_runtime_gate.c" \
    "$repo_root/common_include/wavevm_runtime_dispatch.c" \
    "$repo_root/common_include/wavevm_control.c" \
    "$repo_root/common_include/wavevm_route_runtime.c" \
    "$repo_root/common_include/wavevm_envelope.c" \
    "$repo_root/common_include/wavevm_vcpu_handoff.c" \
    "$repo_root/common_include/wavevm_vcpu_handoff_cache.c" \
    "$repo_root/common_include/wavevm_x86_context.c" \
    "$repo_root/common_include/wavevm_executor_abi.c" \
    "$repo_root/node_runtime/kvm_page_cache.c" \
    "$repo_root/node_runtime/executor_bridge.c" \
    "$repo_root/tests/control-plane/test_executor_bridge.c" \
    -pthread -o "$tmpdir/test_executor_bridge"
"$tmpdir/test_executor_bridge"
