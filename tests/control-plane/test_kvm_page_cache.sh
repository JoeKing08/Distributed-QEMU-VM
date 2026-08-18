#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/wavevm-kvm-cache.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

gcc -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L \
    -I"$repo_root/common_include" -I"$repo_root/node_runtime" \
    "$repo_root/node_runtime/kvm_page_cache.c" \
    "$repo_root/tests/control-plane/test_kvm_page_cache.c" \
    -pthread -o "$tmpdir/test_kvm_page_cache"
"$tmpdir/test_kvm_page_cache"
