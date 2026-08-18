#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

cc -std=c11 -Wall -Wextra -Werror \
    -I"$repo_root/common_include" \
    "$repo_root/common_include/wavevm_sha256.c" \
    "$repo_root/common_include/wavevm_envelope.c" \
    "$repo_root/common_include/wavevm_memory.c" \
    "$repo_root/common_include/wavevm_local_memory.c" \
    "$repo_root/tests/control-plane/test_local_memory.c" \
    -pthread -o "$tmpdir/test_local_memory"
"$tmpdir/test_local_memory"
