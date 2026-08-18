#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/wavevm-gateway-route-control.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

make -C "$repo_root/gateway_service" wavevm_gateway

gcc -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L \
    -I"$repo_root/common_include" \
    "$repo_root/common_include/wavevm_sha256.c" \
    "$repo_root/common_include/wavevm_canonical.c" \
    "$repo_root/common_include/wavevm_identity.c" \
    "$repo_root/common_include/wavevm_manifest.c" \
    "$repo_root/common_include/wavevm_lifecycle.c" \
    "$repo_root/common_include/wavevm_control.c" \
    "$repo_root/common_include/wavevm_runtime_gate.c" \
    "$repo_root/common_include/wavevm_envelope.c" \
    "$repo_root/common_include/wavevm_route_delivery.c" \
    "$repo_root/tests/control-plane/test_gateway_route_control_integration.c" \
    -pthread -o "$tmpdir/test_gateway_route_control_integration"

"$tmpdir/test_gateway_route_control_integration" \
    "$repo_root/gateway_service/wavevm_gateway"
