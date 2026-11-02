cmake_minimum_required(VERSION 3.20)
project(prometheus64 VERSION 0.2.7 LANGUAGES C CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings for this project's own code only (not for SDL, which is built alongside).
add_library(p64_warnings INTERFACE)
if(MSVC)
  target_compile_options(p64_warnings INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive->")
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(p64_warnings INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>")
endif()

# One self-contained .exe: no Visual C++ runtime to install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PROMETHEUS64_BUILD_APP "Build the Prometheus 64 app (downloads SDL3)" ON)

include(CTest)

add_subdirectory(savekit)
add_subdirectory(tools/n64save)
if(PROMETHEUS64_BUILD_APP)
  add_subdirectory(apps/prometheus64)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
