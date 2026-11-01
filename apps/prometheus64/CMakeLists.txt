include(FetchContent)

# SDL3 (zlib licence): window, OpenGL context, audio, gamepads, file dialogs. Built statically.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.8
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3)

add_subdirectory(archives)
add_subdirectory(net)
# The GBA core needs a GNU compiler; with Microsoft's it is built apart (tools/build-gba-core-windows.sh).
if(NOT MSVC)
  add_subdirectory(third_party/gpsp)
endif()

# The relay's code, built into the app so it can install it on a player's Cloudflare account.
file(READ "${PROJECT_SOURCE_DIR}/relay/src/index.js" P64_RELAY_JS)
configure_file(src/relay_script.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/relay_script.hpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/relay/src/index.js")

# xBRZ (Zenju, GNU GPL version 3), the GBA Smooth look's filter: VBA-M's vectorised version of
# 1.9, kept as it is in third_party/xbrz (see its README). Its own warnings, not this project's.
add_library(p64_xbrz STATIC third_party/xbrz/xbrz.cpp)
target_include_directories(p64_xbrz PUBLIC third_party/xbrz)

add_executable(prometheus64 WIN32
  src/app.cpp
  src/archive.cpp
  src/cloudflare.cpp
  src/app_backup.cpp
  src/app_online.cpp
  src/backup.cpp
  src/google.cpp
  src/json.cpp
  src/net.cpp
  src/netplay.cpp
  src/link.cpp
  src/local_link.cpp
  src/gift_station.cpp
  src/cable_station.cpp
  src/gift_catalog.cpp
  src/pokemon_hints.cpp
  src/pokemon_stats.cpp
  src/audio.cpp
  src/time_stretch.cpp
  src/core.cpp
  src/gl.cpp
  src/input.cpp
  src/library.cpp
  src/main.cpp
  src/secret_store.cpp
  src/settings.cpp
  src/ui.cpp
  src/video.cpp
)
set_target_properties(prometheus64 PROPERTIES OUTPUT_NAME ProjectPrometheus)
target_compile_definitions(prometheus64 PRIVATE P64_VERSION="${PROJECT_VERSION}")
# Google Drive backup: the app's Google client ID (and the installed-app client "secret", which
# Google doesn't treat as secret). Given by CI from repository secrets, never stored in the repo.
set(P64_GOOGLE_CLIENT_ID "" CACHE STRING "Google OAuth client ID for Drive backup")
set(P64_GOOGLE_CLIENT_SECRET "" CACHE STRING "Google OAuth client secret (installed app)")
target_compile_definitions(prometheus64 PRIVATE P64_GOOGLE_CLIENT_ID="${P64_GOOGLE_CLIENT_ID}"
                           P64_GOOGLE_CLIENT_SECRET="${P64_GOOGLE_CLIENT_SECRET}")
target_include_directories(prometheus64 PRIVATE src third_party/libretro third_party/stb
                           "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(prometheus64 PRIVATE savekit SDL3::SDL3-static p64_archive p64_curl p64_xbrz p64_warnings)
# The GBA core is built with the app and goes in "cores" next to it (the N64 core is put there by
# tools/build-core-windows.sh, or by hand on other systems).
if(TARGET gpsp_libretro)
  add_dependencies(prometheus64 gpsp_libretro)
  add_custom_command(TARGET prometheus64 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:prometheus64>/cores"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:gpsp_libretro>" "$<TARGET_FILE_DIR:prometheus64>/cores/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/third_party/gpsp/GBA-CORE-SOURCE.txt"
            "$<TARGET_FILE_DIR:prometheus64>/cores/")
endif()

# Online play without the emulator: two players in one process, through a relay, at the game's
# speed. Measures pauses and delays from the network alone (tools/netsim.cpp). Not built by default:
#   cmake --build build --target p64_netsim
add_executable(p64_netsim EXCLUDE_FROM_ALL tools/netsim.cpp src/netplay.cpp src/net.cpp src/json.cpp)
target_include_directories(p64_netsim PRIVATE src)
target_link_libraries(p64_netsim PRIVATE SDL3::SDL3-static p64_curl)

# A GBA game without a window, driven by a script: the test bench for the event gift station
# (tools/giftsim.cpp). Not built by default:
#   cmake --build build --target p64_giftsim
add_executable(p64_giftsim EXCLUDE_FROM_ALL tools/giftsim.cpp src/core.cpp src/local_link.cpp src/gift_station.cpp src/cable_station.cpp src/gift_catalog.cpp src/pokemon_hints.cpp src/pokemon_stats.cpp)
target_include_directories(p64_giftsim PRIVATE src third_party/libretro)
target_link_libraries(p64_giftsim PRIVATE SDL3::SDL3-static)

if(WIN32)
  target_link_libraries(prometheus64 PRIVATE ws2_32 advapi32)  # the sign-in's loopback, Credential Manager
  configure_file(windows/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)
  target_sources(prometheus64 PRIVATE windows/app.rc)
  target_include_directories(prometheus64 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" windows)
  if(MSVC)
    target_sources(prometheus64 PRIVATE windows/app.manifest)
  else()
    target_sources(prometheus64 PRIVATE windows/manifest.rc)
    target_link_options(prometheus64 PRIVATE -static)
  endif()
endif()

# Fonts and the icon live in "assets" next to the program.
add_custom_command(TARGET prometheus64 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:prometheus64>/assets/fonts"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/FiraSans-Regular.ttf"
          "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/FiraSans-SemiBold.ttf"
          "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/OFL.txt"
          "$<TARGET_FILE_DIR:prometheus64>/assets/fonts/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${PROJECT_SOURCE_DIR}/assets/branding/prometheus/png/prometheus-icon-512.png"
          "$<TARGET_FILE_DIR:prometheus64>/assets/icon.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${PROJECT_SOURCE_DIR}/assets/branding/prometheus/png/prometheus-icon-small-256.png"
          "$<TARGET_FILE_DIR:prometheus64>/assets/icon-small.png")

# Binding logic of the Controls screen, tested without a window or devices.
if(BUILD_TESTING)
  add_executable(input_tests tests/input_tests.cpp src/input.cpp src/settings.cpp)
  target_include_directories(input_tests PRIVATE src third_party/libretro)
  target_link_libraries(input_tests PRIVATE SDL3::SDL3-static p64_warnings)
  add_test(NAME controls_bindings COMMAND input_tests)
  set_tests_properties(controls_bindings PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # Fast-forward sound: sped up, pitch kept.
  add_executable(time_stretch_tests tests/time_stretch_tests.cpp src/time_stretch.cpp)
  target_include_directories(time_stretch_tests PRIVATE src)
  target_link_libraries(time_stretch_tests PRIVATE p64_warnings)
  add_test(NAME time_stretch COMMAND time_stretch_tests)
add_executable(pokemon_stats_tests tests/pokemon_stats_tests.cpp src/pokemon_stats.cpp)
target_include_directories(pokemon_stats_tests PRIVATE src)
target_link_libraries(pokemon_stats_tests PRIVATE p64_warnings)
add_test(NAME pokemon_stats COMMAND pokemon_stats_tests)
endif()
