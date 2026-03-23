# Tests

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Unit tests
add_executable(pebble_tests
    unit/test_fixed_point.cpp
    unit/test_memory.cpp
)

target_link_libraries(pebble_tests PRIVATE
    pebble_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(pebble_tests)
