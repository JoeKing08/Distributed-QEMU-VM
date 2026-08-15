#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/wavevm-canonical-record.XXXXXX")

cleanup() {
    rm -rf "$tmpdir"
}
trap cleanup EXIT

gcc -Wall -Wextra -Werror -std=c11 -I"$repo_root/common_include" \
    "$repo_root/common_include/wavevm_sha256.c" \
    "$repo_root/common_include/wavevm_canonical.c" \
    "$repo_root/tests/control-plane/test_canonical_record.c" \
    -o "$tmpdir/test_canonical_record"
"$tmpdir/test_canonical_record"
