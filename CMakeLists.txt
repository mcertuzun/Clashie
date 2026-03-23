cmake_minimum_required(VERSION 3.20)
project(MiniClash
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Isometric base-building & attack simulation on Pebble Engine"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults to Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PEBBLE_PLATFORM_MACOS ON)
    add_compile_definitions(PEBBLE_PLATFORM_MACOS=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PEBBLE_PLATFORM_WINDOWS ON)
    add_compile_definitions(PEBBLE_PLATFORM_WINDOWS=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PEBBLE_PLATFORM_IOS ON)
    add_compile_definitions(PEBBLE_PLATFORM_IOS=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PEBBLE_PLATFORM_ANDROID ON)
    add_compile_definitions(PEBBLE_PLATFORM_ANDROID=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(PEBBLE_PLATFORM_WEB ON)
    add_compile_definitions(PEBBLE_PLATFORM_WEB=1)
endif()

# Debug defines
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(PEBBLE_DEBUG=1)
endif()

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Subdirectories
add_subdirectory(engine)
add_subdirectory(game)

# Tests (optional)
option(PEBBLE_BUILD_TESTS "Build unit and determinism tests" ON)
if(PEBBLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
