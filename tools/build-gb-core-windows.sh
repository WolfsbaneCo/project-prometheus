#!/usr/bin/env bash
# Cross-compiles the Game Boy and Game Boy Color emulator core (Gambatte, GPLv2) for 64-bit Windows
# with MinGW-w64, from the app's own CMake file for it (apps/prometheus64/third_party/gambatte, which
# pins the commit and holds the link cable changes).
#
#   tools/build-gb-core-windows.sh <output folder>
#
# Needs: cmake and the posix-thread MinGW-w64 compilers (Ubuntu: mingw-w64).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?usage: build-gb-core-windows.sh <output folder>}
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=${GB_CORE_WORK_DIR:-$(mktemp -d)}

cmake -S "$root/apps/prometheus64/third_party/gambatte" -B "$work" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$root/tools/mingw-w64.cmake"
cmake --build "$work" -j"$(nproc)"

cp "$work/gambatte_libretro.dll" "$work/GB-CORE-SOURCE.txt" "$out/"

if x86_64-w64-mingw32-objdump -p "$out/gambatte_libretro.dll" | grep -i 'DLL Name' | grep -qiv \
    -e kernel32 -e msvcrt -e user32 -e advapi32 -e shell32 -e ws2_32 -e 'api-ms-win'; then
  echo "The Game Boy core depends on a DLL Windows does not ship:" >&2
  x86_64-w64-mingw32-objdump -p "$out/gambatte_libretro.dll" | grep 'DLL Name' >&2
  exit 1
fi
echo "Game Boy core built: $out/gambatte_libretro.dll"
