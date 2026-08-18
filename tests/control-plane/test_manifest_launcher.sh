#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/wavevm-manifest-launcher.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

runtime="$tmpdir/runtime"
qemu="$tmpdir/qemu"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'if [[ " $* " == *" --print-launch-env "* ]]; then' \
    '    printf "export WVM_RUNTIME_SOCKET=/tmp/wvm-test.sock\\n"' \
    '    printf "export WVM_RUNTIME_READY_FILE=/tmp/wvm-test.ready\\n"' \
    '    printf "export WVM_SHM_FILE=/wavevm_ram_test\\n"' \
    '    printf "export WVM_RUNTIME_MANIFEST_PATH=/tmp/admitted.manifest\\n"' \
    '    exit 0' \
    'fi' \
    'touch "$WVM_RUNTIME_READY_FILE"' \
    'python3 - "$WVM_RUNTIME_SOCKET" <<"PY"' \
    'import socket, sys, time' \
    'path = sys.argv[1]' \
    'server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)' \
    'server.bind(path)' \
    'server.listen(1)' \
    'time.sleep(30)' \
    'PY' \
    >"$runtime"
printf '%s\n' \
    '#!/usr/bin/env bash' \
    'printf "%s\\n" "$WVM_RUNTIME_SOCKET" >"$QEMU_ENV_OUT"' \
    'printf "%s\\n" "$WVM_SHM_FILE" >>"$QEMU_ENV_OUT"' \
    'printf "%s\\n" "$@" >"$QEMU_ARGS_OUT"' \
    >"$qemu"
chmod 700 "$runtime" "$qemu"

QEMU_ENV_OUT="$tmpdir/env" QEMU_ARGS_OUT="$tmpdir/args" \
    "$repo_root/scripts/wavevm_manifest_qemu.sh" \
    --runtime "$runtime" --manifest "$tmpdir/manifest" \
    --node-instance 7 --qemu "$qemu" -- -machine pc-test -m 128

test "$(sed -n '1p' "$tmpdir/env")" = "/tmp/wvm-test.sock"
test "$(sed -n '2p' "$tmpdir/env")" = "/wavevm_ram_test"
test "$(sed -n '1p' "$tmpdir/args")" = "-machine"
test "$(sed -n '2p' "$tmpdir/args")" = "pc-test"
test "$(sed -n '3p' "$tmpdir/args")" = "-m"
test "$(sed -n '4p' "$tmpdir/args")" = "128"
test ! -e /tmp/wvm-test.sock
test ! -e /tmp/wvm-test.ready

printf 'manifest-launcher tests: PASS\n'
