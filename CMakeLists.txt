cmake_minimum_required(VERSION 3.20)
project(prometheus64 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(savekit)
add_subdirectory(tools/n64save)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
