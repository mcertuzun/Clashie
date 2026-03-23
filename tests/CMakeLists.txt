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

# MiniClash simulation tests
add_executable(miniclash_tests
    unit/test_simulation.cpp
    ${CMAKE_SOURCE_DIR}/game/src/simulation.cpp
    ${CMAKE_SOURCE_DIR}/game/src/building_data.cpp
    ${CMAKE_SOURCE_DIR}/game/src/troop_data.cpp
)

target_include_directories(miniclash_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/game/include
)

target_link_libraries(miniclash_tests PRIVATE pebble GTest::gtest_main)
gtest_discover_tests(miniclash_tests)
