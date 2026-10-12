#!/usr/bin/env bash
# Checks the cross-built proxy: it exports DirectInputCreateA and imports only system DLLs.
set -euo pipefail
dll="${1:-build-win/dinput.dll}"
[ -f "$dll" ] || { echo "FAIL: $dll not found"; exit 1; }
# Read objdump once: `objdump | grep -q` fails under pipefail when grep exits early (SIGPIPE).
info=$(i686-w64-mingw32-objdump -p "$dll")
if ! grep -q -E '^\s+\[ *[0-9]+\] DirectInputCreateA$' <<<"$info"; then
    echo "FAIL: DirectInputCreateA is not exported"
    exit 1
fi
imports=$(awk '/DLL Name:/ {print $3}' <<<"$info")
bad=$(grep -v -i -E '^(KERNEL32|USER32|WS2_32|msvcrt)\.dll$' <<<"$imports" || true)
if [ -n "$bad" ]; then
    echo "FAIL: unexpected imports: $bad"
    exit 1
fi
echo "OK: $dll exports DirectInputCreateA; imports: $(echo $imports)"
