#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/wavevm-memory-v1.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

gcc -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L \
    -I"$repo_root/common_include" \
    "$repo_root/common_include/wavevm_sha256.c" \
    "$repo_root/common_include/wavevm_envelope_v1.c" \
    "$repo_root/common_include/wavevm_memory_v1.c" \
    "$repo_root/tests/control-plane/test_memory_v1.c" \
    -o "$tmpdir/test_memory_v1"
"$tmpdir/test_memory_v1"
