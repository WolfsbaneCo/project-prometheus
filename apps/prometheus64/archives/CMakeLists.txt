# Games packed in .zip, .7z and .rar files: libarchive (BSD-2-Clause) reads them, with zlib (zlib
# licence) for zip's deflate and liblzma from XZ Utils (0BSD) for 7z's LZMA. All three are built
# here as static libraries, so the app needs no extra DLLs. This directory has its own scope: the
# settings below do not leak into the rest of the build.
include(FetchContent)

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# zlib: compiled from its sources directly (its own CMake build renames files in the source tree).
FetchContent_Declare(p64_zlib_src
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG v1.3.1
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake-build)
FetchContent_MakeAvailable(p64_zlib_src)
set(Z ${p64_zlib_src_SOURCE_DIR})
add_library(p64_zlib STATIC
  ${Z}/adler32.c ${Z}/crc32.c ${Z}/inffast.c ${Z}/inflate.c ${Z}/inftrees.c ${Z}/zutil.c
  ${Z}/deflate.c ${Z}/trees.c ${Z}/compress.c ${Z}/uncompr.c)
target_include_directories(p64_zlib PUBLIC ${Z})
if(MSVC)
  target_compile_options(p64_zlib PRIVATE /W0)
else()
  target_compile_options(p64_zlib PRIVATE -w)
  target_compile_definitions(p64_zlib PRIVATE HAVE_UNISTD_H)
endif()
add_library(ZLIB::ZLIB ALIAS p64_zlib)

# liblzma: decoders only matter to us, but its build has no switch for that; the tools are off.
set(XZ_NLS OFF)
set(XZ_DOC OFF)
set(XZ_TOOL_XZ OFF)
set(XZ_TOOL_XZDEC OFF)
set(XZ_TOOL_LZMADEC OFF)
set(XZ_TOOL_LZMAINFO OFF)
set(XZ_TOOL_SCRIPTS OFF)
set(XZ_TOOL_SYMLINKS OFF)
set(XZ_TOOL_SYMLINKS_LZMA OFF)
FetchContent_Declare(p64_xz
  GIT_REPOSITORY https://github.com/tukaani-project/xz.git
  GIT_TAG v5.8.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(p64_xz)
add_library(LibLZMA::LibLZMA ALIAS liblzma)

# libarchive finds zlib and liblzma with CMake's find modules. Point those at the libraries above:
# the find modules keep variables that are already set, and keep the ZLIB::ZLIB and
# LibLZMA::LibLZMA targets defined here. The link checks they would run need built libraries, so
# their answers are given up front.
set(ZLIB_INCLUDE_DIR ${Z} CACHE PATH "" FORCE)
set(ZLIB_LIBRARY p64_zlib CACHE STRING "" FORCE)
set(LIBLZMA_INCLUDE_DIR ${p64_xz_SOURCE_DIR}/src/liblzma/api CACHE PATH "" FORCE)
set(LIBLZMA_LIBRARY liblzma CACHE STRING "" FORCE)
set(LIBLZMA_HAS_AUTO_DECODER 1 CACHE INTERNAL "")
set(LIBLZMA_HAS_EASY_ENCODER 1 CACHE INTERNAL "")
set(LIBLZMA_HAS_LZMA_PRESET 1 CACHE INTERNAL "")
add_compile_definitions(LZMA_API_STATIC)

foreach(opt OPENSSL MBEDTLS NETTLE LIBB2 LZ4 LZO ZSTD BZip2 LIBXML2 EXPAT WIN32_XMLLITE PCREPOSIX
            PCRE2POSIX LIBGCC CNG TAR CPIO CAT UNZIP XATTR ACL ICONV TEST INSTALL WERROR)
  set(ENABLE_${opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(MSVC_USE_STATIC_CRT ON CACHE BOOL "" FORCE)
set(POSIX_REGEX_LIB NONE CACHE STRING "" FORCE)  # only libarchive's own command-line tools use it
FetchContent_Declare(p64_libarchive
  GIT_REPOSITORY https://github.com/libarchive/libarchive.git
  GIT_TAG v3.8.4
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(p64_libarchive)

add_library(p64_archive INTERFACE)
target_link_libraries(p64_archive INTERFACE archive_static p64_zlib liblzma)
target_compile_definitions(p64_archive INTERFACE LIBARCHIVE_STATIC)
