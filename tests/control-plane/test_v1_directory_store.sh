#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/wavevm-directory-store-v1.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

gcc -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L \
    -I"$repo_root/common_include" -I"$repo_root/node_runtime" \
    "$repo_root/node_runtime/v1_directory_store.c" \
    "$repo_root/tests/control-plane/test_v1_directory_store.c" \
    -pthread -o "$tmpdir/test_v1_directory_store"
"$tmpdir/test_v1_directory_store"
