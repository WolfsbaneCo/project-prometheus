add_executable(n64save main.cpp)
target_link_libraries(n64save PRIVATE savekit)
target_link_libraries(n64save PRIVATE p64_warnings)
