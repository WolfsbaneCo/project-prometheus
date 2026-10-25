add_library(savekit STATIC
  src/bytes.cpp
  src/formats.cpp
  src/inspect.cpp
  src/library.cpp
  src/rom_header.cpp
  src/save_set.cpp
  src/sha256.cpp
)
target_include_directories(savekit PUBLIC include)
target_link_libraries(savekit PRIVATE p64_warnings)
