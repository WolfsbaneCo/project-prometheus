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

# The relay's code, built into the app so it can install it on a player's Cloudflare account.
file(READ "${PROJECT_SOURCE_DIR}/relay/src/index.js" P64_RELAY_JS)
configure_file(src/relay_script.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/relay_script.hpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/relay/src/index.js")

add_executable(prometheus64 WIN32
  src/app.cpp
  src/archive.cpp
  src/cloudflare.cpp
  src/app_online.cpp
  src/json.cpp
  src/net.cpp
  src/netplay.cpp
  src/audio.cpp
  src/core.cpp
  src/gl.cpp
  src/input.cpp
  src/library.cpp
  src/main.cpp
  src/settings.cpp
  src/ui.cpp
  src/video.cpp
)
set_target_properties(prometheus64 PROPERTIES OUTPUT_NAME Prometheus64)
target_compile_definitions(prometheus64 PRIVATE P64_VERSION="${PROJECT_VERSION}")
target_include_directories(prometheus64 PRIVATE src third_party/libretro third_party/stb
                           "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(prometheus64 PRIVATE savekit SDL3::SDL3-static p64_archive p64_curl p64_warnings)

if(WIN32)
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
          "${PROJECT_SOURCE_DIR}/assets/branding/prometheus64/png/prometheus64-icon-512.png"
          "$<TARGET_FILE_DIR:prometheus64>/assets/icon.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${PROJECT_SOURCE_DIR}/assets/branding/prometheus64/png/prometheus64-icon-small-256.png"
          "$<TARGET_FILE_DIR:prometheus64>/assets/icon-small.png")

# Binding logic of the Controls screen, tested without a window or devices.
if(BUILD_TESTING)
  add_executable(input_tests tests/input_tests.cpp src/input.cpp src/settings.cpp)
  target_include_directories(input_tests PRIVATE src third_party/libretro)
  target_link_libraries(input_tests PRIVATE SDL3::SDL3-static p64_warnings)
  add_test(NAME controls_bindings COMMAND input_tests)
  set_tests_properties(controls_bindings PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
endif()
