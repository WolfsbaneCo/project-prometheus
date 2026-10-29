# The GBA emulator core: gpSP for libretro (GNU GPL version 2), at a pinned commit. Built here from
# its source files with this project's compiler settings (the same ones gpSP's own Makefile uses on
# PC: the x86 dynamic recompiler, RGB565 output), into gpsp_libretro.so / .dll next to the app.
# gpSP is chosen for its networked link play: the Pokemon generation 3 link cable mode and the GBA
# Wireless Adapter, where each console runs on its own and only link messages travel.
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

enable_language(ASM)
add_library(gpsp_libretro SHARED
  ${G}/main.c ${G}/gba_memory.c ${G}/savestate.c ${G}/input.c ${G}/sound.c ${G}/cheats.c ${G}/memmap.c
  ${G}/serial.c ${G}/gbp.c ${G}/rfu.c ${G}/serial_proto.c ${G}/libretro/libretro.c ${G}/gba_cc_lut.c
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
Includes the open source GBA BIOS replacement by Normmatt and the VBA/VBA-M team (GPL version 2).
")
