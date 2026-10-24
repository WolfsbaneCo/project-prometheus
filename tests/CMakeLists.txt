add_executable(roundtrip_tests roundtrip_tests.cpp)
target_link_libraries(roundtrip_tests PRIVATE savekit)
add_test(NAME save_format_roundtrip COMMAND roundtrip_tests)

add_executable(savekit_tests savekit_tests.cpp)
target_link_libraries(savekit_tests PRIVATE savekit)
add_test(NAME savekit COMMAND savekit_tests)
