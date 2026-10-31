#!/usr/bin/env bash
# Cross-compiles the GBA emulator core (gpSP, GPLv2) for 64-bit Windows with MinGW-w64, from the
# app's own CMake file for it (apps/prometheus64/third_party/gpsp, which pins the commit).
#
#   tools/build-gba-core-windows.sh <output folder>
#
# Needs: cmake and the posix-thread MinGW-w64 compilers (Ubuntu: mingw-w64).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?usage: build-gba-core-windows.sh <output folder>}
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=${GBA_CORE_WORK_DIR:-$(mktemp -d)}

cmake -S "$root/apps/prometheus64/third_party/gpsp" -B "$work" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$root/tools/mingw-w64.cmake"
cmake --build "$work" -j"$(nproc)"

cp "$work/gpsp_libretro.dll" "$work/GBA-CORE-SOURCE.txt" "$out/"

if x86_64-w64-mingw32-objdump -p "$out/gpsp_libretro.dll" | grep -i 'DLL Name' | grep -qiv \
    -e kernel32 -e msvcrt -e user32 -e advapi32 -e shell32 -e ws2_32 -e 'api-ms-win'; then
  echo "The GBA core depends on a DLL Windows does not ship:" >&2
  x86_64-w64-mingw32-objdump -p "$out/gpsp_libretro.dll" | grep 'DLL Name' >&2
  exit 1
fi
echo "GBA core built: $out/gpsp_libretro.dll"
