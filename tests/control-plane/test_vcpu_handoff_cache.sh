#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/wavevm-vcpu-handoff-cache.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

gcc -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L \
    -I"$repo_root/common_include" \
    "$repo_root/common_include/wavevm_sha256.c" \
    "$repo_root/common_include/wavevm_envelope.c" \
    "$repo_root/common_include/wavevm_vcpu_handoff.c" \
    "$repo_root/common_include/wavevm_vcpu_handoff_cache.c" \
    "$repo_root/tests/control-plane/test_vcpu_handoff_cache.c" \
    -pthread -o "$tmpdir/test_vcpu_handoff_cache"
"$tmpdir/test_vcpu_handoff_cache"
