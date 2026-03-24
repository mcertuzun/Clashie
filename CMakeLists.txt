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

# Emscripten / WebGL build flags
if(PEBBLE_PLATFORM_WEB)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # WebGL 2.0 (= OpenGL ES 3.0), ASYNCIFY so the normal while-loop works,
    # allow memory to grow, start with 128 MB.
    set(EMSCRIPTEN_LINK_FLAGS
        "-sUSE_WEBGL2=1"
        "-sFULL_ES3=1"
        "-sASYNCIFY=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=134217728"
        "-sASYNCIFY_STACK_SIZE=65536"
    )
    # Join list into a space-separated string for linker flags
    list(JOIN EMSCRIPTEN_LINK_FLAGS " " EMSCRIPTEN_LINK_FLAGS_STR)

    # Linker flags applied to all executables
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EMSCRIPTEN_LINK_FLAGS_STR} -sSTACK_SIZE=1048576")

    # Disable -Werror for web builds (emscripten warnings)
    string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "-Wpedantic" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# Debug defines
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(PEBBLE_DEBUG=1)
endif()

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
elseif(PEBBLE_PLATFORM_WEB)
    add_compile_options(-Wall -Wextra)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Subdirectories
add_subdirectory(engine)
add_subdirectory(game)

# Tests (optional, disabled for web builds)
option(PEBBLE_BUILD_TESTS "Build unit and determinism tests" ON)
if(PEBBLE_BUILD_TESTS AND NOT PEBBLE_PLATFORM_WEB)
    enable_testing()
    add_subdirectory(tests)
endif()
