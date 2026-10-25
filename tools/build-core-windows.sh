#!/usr/bin/env bash
# Cross-compiles the emulator core (Mupen64Plus-Next, GPLv2) for 64-bit Windows with MinGW-w64.
#
#   tools/build-core-windows.sh <output folder>
#
# Needs: git, make, nasm and the posix-thread MinGW-w64 compilers (Ubuntu: mingw-w64 nasm).
# The core is pinned to one commit so every release ships the same, known source; that commit is
# written next to the DLL in CORE-SOURCE.txt, which the GPL source offer points to.
set -euo pipefail

CORE_REPO=https://github.com/libretro/mupen64plus-libretro-nx
CORE_COMMIT=15bada85906593376e8fd697f1b0b3af3a20f512

out=${1:?usage: build-core-windows.sh <output folder>}
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=${CORE_WORK_DIR:-$(mktemp -d)}

if [ ! -d "$work/src/.git" ]; then
  git init -q "$work/src"
  git -C "$work/src" remote add origin "$CORE_REPO"
fi
git -C "$work/src" fetch -q --depth 1 origin "$CORE_COMMIT"
git -C "$work/src" checkout -q --force FETCH_HEAD

# LDFLAGS=-static (from the environment, so the Makefile still appends its own flags) links the
# MinGW thread library in: the DLL then needs only system libraries.
(cd "$work/src" && LDFLAGS=-static MSYSTEM=MINGW64 make -j"$(nproc)" platform=win \
  CC=x86_64-w64-mingw32-gcc-posix CXX=x86_64-w64-mingw32-g++-posix AR=x86_64-w64-mingw32-ar \
  HAVE_PARALLEL_RDP=1 HAVE_THR_AL=1)

cp "$work/src/mupen64plus_next_libretro.dll" "$out/"
cat > "$out/CORE-SOURCE.txt" <<EOF
Emulator core: Mupen64Plus-Next (libretro), GNU General Public License version 2.
Source: $CORE_REPO
Commit: $CORE_COMMIT
Built with: make platform=win HAVE_PARALLEL_RDP=1 HAVE_THR_AL=1 (MinGW-w64, static runtime)
EOF

if x86_64-w64-mingw32-objdump -p "$out/mupen64plus_next_libretro.dll" | grep -i 'DLL Name' | grep -qiv \
    -e kernel32 -e msvcrt -e opengl32 -e user32 -e gdi32 -e winmm -e advapi32 -e shell32 -e ws2_32; then
  echo "The core depends on a DLL Windows does not ship:" >&2
  x86_64-w64-mingw32-objdump -p "$out/mupen64plus_next_libretro.dll" | grep 'DLL Name' >&2
  exit 1
fi
echo "Core built: $out/mupen64plus_next_libretro.dll"
