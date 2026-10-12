#!/usr/bin/env bash
# Builds the Windows DLL, checks it and packs dist/out/uaro-pad-<version>.zip.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-win -j 8 >/dev/null
tools/check_dll.sh build-win/dinput.dll
version=$(sed -n 's/.*return "\([0-9.]*\)".*/\1/p' src/core/version.cpp)
mkdir -p dist/out
out="dist/out/uaro-pad-${version}.zip"
rm -f "$out"
files=(build-win/dinput.dll dist/uaro-pad.ini dist/README.ru.md)
for f in "${files[@]}"; do
    # The zipfile CLI silently skips missing paths, so check them first.
    [ -f "$f" ] || { echo "FAIL: missing $f"; exit 1; }
done
python3 -I -m zipfile -c "$out" "${files[@]}"
echo "packed $out"
