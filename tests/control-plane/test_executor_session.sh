#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "$0")/../.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror \
  -I"$root_dir/common_include" \
  "$root_dir/tests/control-plane/test_executor_session.c" \
  "$root_dir/common_include/wavevm_executor_session.c" \
  -o "$tmp_dir/test_executor_session"

"$tmp_dir/test_executor_session"
