#!/usr/bin/env bash
# Cross-compiles the PSP emulator core (PPSSPP, libretro; GPL version 2 or later) for 64-bit
# Windows with MinGW-w64, and gathers the files it needs beside it (the PSP's fonts, its list of
# game fixes).
#
#   tools/build-psp-core-windows.sh <output folder>
#
# Needs: git, cmake, make and the posix-thread MinGW-w64 compilers (Ubuntu: mingw-w64).
# The core is pinned to one release so every build ships the same, known source; it is written
# next to the DLL in PSP-CORE-SOURCE.txt, which the GPL source offer points to.
#
# Three things PPSSPP's own Windows builds (Visual Studio) don't need:
# - FFmpeg: PPSSPP plays the games' videos and some of their music with its own FFmpeg (the
#   "ffmpeg" submodule). The copy it ships built for Windows only links with Visual Studio, so
#   it is built here from that same source, with the decoders PPSSPP's own builds switch on.
# - Header names: the sources name Windows headers as Visual Studio users write them
#   ("Windows.h", "D3Dcommon.h"); on Windows any spelling opens the file, on Linux only the
#   exact one. A folder of one-line headers, made here, passes each on to its real name.
# - Two small adjustments, each explained below where it is made.
set -euo pipefail

PSP_REPO=https://github.com/hrydgard/ppsspp.git
PSP_TAG=v1.19.3
PSP_COMMIT=e49c0bd8836a8a8f678565357773386f1174d3f5

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?usage: build-psp-core-windows.sh <output folder>}
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=${PSP_CORE_WORK_DIR:-$(mktemp -d)}
jobs=${PSP_CORE_JOBS:-$(nproc)}
src=$work/src

if [ ! -d "$src/.git" ]; then
  git clone -q --depth 1 --branch "$PSP_TAG" "$PSP_REPO" "$src"
fi
if [ "$(git -C "$src" rev-parse HEAD)" != "$PSP_COMMIT" ]; then
  echo "PPSSPP $PSP_TAG is no longer commit $PSP_COMMIT: check the release before building it" >&2
  exit 1
fi
git -C "$src" submodule update -q --init --depth 1 --recursive

# FFmpeg, static, with PPSSPP's choice of decoders (ffmpeg/linux_x86-64.sh in its source).
if [ ! -f "$work/ffmpeg/lib/libavcodec.a" ]; then
  mkdir -p "$work/ffmpeg-build"
  (cd "$work/ffmpeg-build" && "$src/ffmpeg/configure" --prefix="$work/ffmpeg" \
    --cross-prefix=x86_64-w64-mingw32- --cc=x86_64-w64-mingw32-gcc-posix --target-os=mingw32 --arch=x86_64 \
    --enable-cross-compile --disable-shared --enable-static --enable-pic --disable-yasm \
    --extra-cflags="-D__STDC_CONSTANT_MACROS -O3" --disable-everything --disable-iconv --disable-lzma \
    --disable-bzlib --disable-zlib --disable-avdevice --disable-filters --disable-programs --disable-network \
    --disable-avfilter --disable-postproc --disable-encoders --disable-doc --disable-debug --disable-dxva2 \
    --disable-d3d11va \
    --enable-decoder=h264 --enable-decoder=mpeg4 --enable-decoder=h263 --enable-decoder=h263p \
    --enable-decoder=mpeg2video --enable-decoder=mjpeg --enable-decoder=mjpegb \
    --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=atrac3 --enable-decoder=atrac3p \
    --enable-decoder=mp3 --enable-decoder=pcm_s16le --enable-decoder=pcm_s8 \
    --enable-encoder=ffv1 --enable-encoder=huffyuv --enable-encoder=mpeg4 --enable-encoder=pcm_s16le \
    --enable-demuxer=h264 --enable-demuxer=h263 --enable-demuxer=m4v --enable-demuxer=mpegps \
    --enable-demuxer=mpegvideo --enable-demuxer=avi --enable-demuxer=mp3 --enable-demuxer=aac \
    --enable-demuxer=pmp --enable-demuxer=oma --enable-demuxer=pcm_s16le --enable-demuxer=pcm_s8 \
    --enable-demuxer=wav --enable-muxer=avi \
    --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegvideo --enable-parser=aac \
    --enable-parser=aac_latm --enable-parser=mpegaudio --enable-protocol=file >configure.log 2>&1 \
    && make -j"$jobs" >make.log 2>&1 && make install >install.log 2>&1) || {
    tail -30 "$work/ffmpeg-build/configure.log" "$work/ffmpeg-build/make.log" >&2 || true
    echo "FFmpeg did not build" >&2
    exit 1
  }
fi

# The header names: for every header the sources include by a name MinGW-w64 only has in another
# spelling, a one-line header under that name.
shim=$work/shim
mkdir -p "$shim"
inc=/usr/x86_64-w64-mingw32/include
grep -rhoE '#[[:space:]]*include[[:space:]]*[<"][A-Za-z0-9_.-]+[>"]' "$src/Common" "$src/Core" "$src/GPU" "$src/libretro" \
    "$src/Windows" "$src/UI" "$src/ext/vma" 2>/dev/null |
  sed -E 's/#[[:space:]]*include[[:space:]]*[<"]//; s/[>"]$//' | sort -u | while read -r name; do
  lower=$(printf '%s' "$name" | tr 'A-Z' 'a-z')
  if [ ! -e "$inc/$name" ] && [ -e "$inc/$lower" ]; then printf '#include <%s>\n' "$lower" >"$shim/$name"; fi
done

# The same for one library: PPSSPP links "Version", which MinGW-w64 has as libversion.a.
ln -sf /usr/x86_64-w64-mingw32/lib/libversion.a "$shim/libVersion.a"

# GLEW_STATIC (in the compiler flags below): PPSSPP builds its own copy of the GLEW library into
# the core, and on Windows GLEW's header assumes a separate DLL unless told so.

# Four socket options that Visual Studio's headers name and MinGW-w64's don't, with their Windows
# values: added to the one-line header for "WS2tcpip.h", which PPSSPP's network code includes.
cat >"$shim/WS2tcpip.h" <<'HEADER'
#include <ws2tcpip.h>
#ifndef TCP_KEEPALIVE
#define TCP_KEEPALIVE 3
#endif
#ifndef TCP_MAXSEG
#define TCP_MAXSEG 4
#endif
#ifndef TCP_KEEPCNT
#define TCP_KEEPCNT 16
#endif
#ifndef TCP_KEEPINTVL
#define TCP_KEEPINTVL 17
#endif
HEADER

# The one change to PPSSPP's files: its CMakeLists.txt runs a Windows batch file on every build
# (Windows/git-version-gen.cmd, which stamps the version into the PPSSPP program's own resources).
# It can't run on Linux and the core doesn't use what it makes, so that line is taken out.
stamp='set(WIN_VERSION_CMD COMMAND ${CMAKE_SOURCE_DIR}/Windows/git-version-gen.cmd PPSSPPWindows)'
if grep -qF "$stamp" "$src/CMakeLists.txt"; then
  grep -vF "$stamp" "$src/CMakeLists.txt" >"$work/CMakeLists.txt" && cp "$work/CMakeLists.txt" "$src/CMakeLists.txt"
elif [ -n "$(git -C "$src" status --porcelain CMakeLists.txt)" ]; then
  : # taken out already, by an earlier run in this work folder
else
  echo "PPSSPP's CMakeLists.txt has changed: re-check the version stamp step in $0" >&2
  exit 1
fi

cmake -S "$src" -B "$work/build" -DLIBRETRO=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$root/tools/mingw-w64.cmake" -DFFMPEG_DIR="$work/ffmpeg" \
  -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF \
  -DCMAKE_C_FLAGS="-I$shim -DGLEW_STATIC" -DCMAKE_CXX_FLAGS="-I$shim -DGLEW_STATIC" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L$shim -static -static-libgcc -static-libstdc++" >"$work/cmake.log" 2>&1 || {
  tail -40 "$work/cmake.log" >&2
  exit 1
}
make -C "$work/build" -j"$jobs" ppsspp_libretro >"$work/make.log" 2>&1 || {
  # What went wrong, briefly: compiler errors, then each missing symbol or library once.
  grep -A6 -e ' error: ' -e 'fatal error' "$work/make.log" | head -80 >&2 || true
  grep -o -e 'undefined reference to .*' -e 'cannot find .*' "$work/make.log" | sort | uniq -c | sort -rn | head -150 >&2 || true
  echo "PPSSPP did not build" >&2
  exit 1
}

dll=$(find "$work/build" -name 'ppsspp_libretro.dll' | head -1)
x86_64-w64-mingw32-strip "$dll"
cp "$dll" "$out/ppsspp_libretro.dll"

# What the core reads from its "system" folder (<cores>/PPSSPP): the PSP's fonts (flash0), the
# list of per-game fixes, the dialogs' pictures, the tables of known functions.
rm -rf "$out/PPSSPP"
mkdir -p "$out/PPSSPP"
cp -r "$src/assets/flash0" "$out/PPSSPP/"
cp "$src/assets/compat.ini" "$src/assets/compatvr.ini" "$src/assets/knownfuncs.ini" "$src/assets/langregion.ini" \
  "$src/assets/ppge_atlas.zim" "$src/assets/ppge_atlas.meta" "$out/PPSSPP/"
[ -d "$src/assets/vfpu" ] && cp -r "$src/assets/vfpu" "$out/PPSSPP/"

cat >"$out/PSP-CORE-SOURCE.txt" <<EOF
PSP emulator core: PPSSPP (libretro), GNU General Public License version 2 or later.
Source: $PSP_REPO
Release: $PSP_TAG (commit $PSP_COMMIT), with its submodules
Built with: tools/build-psp-core-windows.sh of Project Prometheus's source (MinGW-w64, static
runtime), which also names the few changes made to build it.
It includes FFmpeg (PPSSPP's own copy, its "ffmpeg" submodule; LGPL version 2.1 or later), built
from that source by the same script, and the PSP fonts and data in the PPSSPP folder, from
PPSSPP's assets.
EOF

# The DLL must stand on its own: the compiler's runtime linked in, only Windows' own DLLs needed.
x86_64-w64-mingw32-objdump -p "$out/ppsspp_libretro.dll" | grep -i 'DLL Name' | sort -u
if x86_64-w64-mingw32-objdump -p "$out/ppsspp_libretro.dll" | grep -i 'DLL Name' | grep -qi \
    -e libstdc++ -e libgcc -e libwinpthread -e avcodec -e avformat -e avutil -e swscale -e swresample; then
  echo "The PSP core depends on a DLL Windows does not ship (listed above)" >&2
  exit 1
fi
echo "PSP core built: $out/ppsspp_libretro.dll"
