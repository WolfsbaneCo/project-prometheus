# The GBA emulator core: gpSP for libretro (GNU GPL version 2), at a pinned commit. Built here from
# its source files with this project's compiler settings (the same ones gpSP's own Makefile uses on
# PC: the x86 dynamic recompiler, RGB565 output), into gpsp_libretro.so / .dll next to the app.
# gpSP is chosen for its networked link play: the Pokemon generation 3 link cable mode and the GBA
# Wireless Adapter, where each console runs on its own and only link messages travel.
#
# Builds on its own too (tools/build-gba-core-windows.sh), for builds whose app compiler cannot
# build gpSP (Microsoft's: gpSP uses GNU assembly and GCC extensions).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(gpsp_libretro LANGUAGES C CXX)
endif()
include(FetchContent)

set(P64_GPSP_REPO https://github.com/libretro/gpsp.git)
set(P64_GPSP_COMMIT 5819380c2ffb0900219d700a382ee68c464ebb99)
FetchContent_Declare(p64_gpsp GIT_REPOSITORY ${P64_GPSP_REPO} GIT_TAG ${P64_GPSP_COMMIT})
FetchContent_GetProperties(p64_gpsp)
if(NOT p64_gpsp_POPULATED)
  FetchContent_Populate(p64_gpsp)  # sources only: gpSP has no CMake build of its own
endif()
set(G "${p64_gpsp_SOURCE_DIR}")
set(C "${G}/libretro/libretro-common")

# One fix to gpSP, made to a copy of serial_proto.c (the Pokemon link cable). On the second Game Boy
# gpSP pretends to be the cable's master and clocks a transfer every 28672 cycles: about 9.8 a frame
# where a real master makes exactly 9 (one 9-word block a frame). The games read one block a frame,
# so the extra one every 11 frames piled up in their 50-block receive queue while the two players
# exchanged keys (the Cable Club rooms): after about 9 seconds the second game reported
# "Communication error" and closed the link, leaving the first on a black screen. The pretend master
# now runs at exactly 9 transfers a frame once connected (1 during the handshake), counted in ninths
# of a cycle so no remainder is lost.
file(READ "${G}/serial_proto.c" P64_SERIAL_PROTO)
set(P64_SERIAL_OLD "  serstate.poke.frcnt += cycles;

  // During handshake we tipically receive one serial transaction per frame.
  // During connection it's ~9 per frame.
  const u32 ev_cycles = (serstate.poke.peer[0].state == STATE_CONNECTED) ?
                        SLAVE_IRQ_CYCLES_C : SLAVE_IRQ_CYCLES_H;

  // Raise an IRQ periodically to pretend there's a master.
  if (netplay_client_id && serstate.poke.frcnt > ev_cycles) {
    serstate.poke.frcnt = 0;
")
set(P64_SERIAL_NEW "  // Project Prometheus: exactly a real master's pace, 9 transfers a frame (280896 cycles) once
  // connected and 1 a frame during the handshake, counted in ninths of a cycle.
  serstate.poke.frcnt += cycles * 9;
  const u32 ev_cycles = (serstate.poke.peer[0].state == STATE_CONNECTED) ? 280896u : 280896u * 9u;

  // Raise an IRQ periodically to pretend there's a master.
  if (netplay_client_id && serstate.poke.frcnt >= ev_cycles) {
    serstate.poke.frcnt -= ev_cycles;
    if (serstate.poke.frcnt >= ev_cycles)
      serstate.poke.frcnt = 0;  // after a pause (or a slower state), no catch-up burst
")
string(REPLACE "${P64_SERIAL_OLD}" "${P64_SERIAL_NEW}" P64_SERIAL_PATCHED "${P64_SERIAL_PROTO}")
if(P64_SERIAL_PATCHED STREQUAL P64_SERIAL_PROTO)
  message(FATAL_ERROR "gpSP's serial_proto.c has changed: re-check the link cable timing fix in ${CMAKE_CURRENT_LIST_FILE}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/serial_proto.c" "${P64_SERIAL_PATCHED}")

enable_language(ASM)
add_library(gpsp_libretro SHARED
  ${G}/main.c ${G}/gba_memory.c ${G}/savestate.c ${G}/input.c ${G}/sound.c ${G}/cheats.c ${G}/memmap.c
  ${G}/serial.c ${G}/gbp.c ${G}/rfu.c ${CMAKE_CURRENT_BINARY_DIR}/serial_proto.c ${G}/libretro/libretro.c ${G}/gba_cc_lut.c
  ${G}/cpu_threaded.c ${G}/video.cc ${G}/cpu.cc ${G}/bios_data.S ${G}/x86/x86_stub.S
  ${C}/compat/compat_posix_string.c ${C}/compat/compat_strl.c ${C}/compat/fopen_utf8.c
  ${C}/encodings/encoding_utf.c ${C}/file/file_path.c ${C}/file/file_path_io.c
  ${C}/streams/file_stream.c ${C}/string/stdstring.c ${C}/time/rtime.c ${C}/vfs/vfs_implementation.c)
set_target_properties(gpsp_libretro PROPERTIES PREFIX "" C_VISIBILITY_PRESET default)
target_include_directories(gpsp_libretro PRIVATE ${G} ${G}/libretro ${C}/include)
target_compile_definitions(gpsp_libretro PRIVATE HAVE_STRINGS_H HAVE_STDINT_H HAVE_INTTYPES_H __LIBRETRO__
  INLINE=inline HAVE_DYNAREC X86_ARCH FRONTEND_SUPPORTS_RGB565 MMAP_JIT_CACHE NDEBUG
  "GIT_VERSION=\"${P64_GPSP_COMMIT}\"")
if(WIN32)
  target_compile_definitions(gpsp_libretro PRIVATE __WIN32__ __WIN32_LIBRETRO__)
  target_link_options(gpsp_libretro PRIVATE -static-libgcc -static-libstdc++ -s)
endif()
# Its own optimisation and warnings, not this project's picky ones.
target_compile_options(gpsp_libretro PRIVATE -O3 -w $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions -std=c++11>)
# .incbin "bios/open_gba_bios.bin" is relative to the source folder.
set_source_files_properties(${G}/bios_data.S PROPERTIES COMPILE_OPTIONS "-I${G};-Wa,-I${G}")
# Only the libretro entry points are visible, as with gpSP's own build.
target_link_options(gpsp_libretro PRIVATE "-Wl,--version-script=${G}/link.T")
if(NOT WIN32)
  target_link_libraries(gpsp_libretro PRIVATE m)
endif()

# Where the GPL source offer points.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/GBA-CORE-SOURCE.txt"
"GBA emulator core: gpSP (libretro), GNU General Public License version 2.
Source: ${P64_GPSP_REPO}
Commit: ${P64_GPSP_COMMIT}
Changed: serial_proto.c, the link cable's timing on the second Game Boy (9 transfers a frame, as a
real master makes); the change is in apps/prometheus64/third_party/gpsp/CMakeLists.txt of
Project Prometheus's source.
Includes the open source GBA BIOS replacement by Normmatt and the VBA/VBA-M team (GPL version 2).
")
