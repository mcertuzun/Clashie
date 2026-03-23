# Pebble Engine

# --- Core library (ECS, memory, fixed-point, asset loader, input) ---
add_library(pebble_core STATIC
    src/core/memory.cpp
    src/core/fixed_point.cpp
    src/core/input.cpp
)

target_include_directories(pebble_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- Platform library (window, file I/O, timing, threading) ---
if(PEBBLE_PLATFORM_MACOS)
    add_library(pebble_platform STATIC
        src/platform/cocoa/window_cocoa.mm
        src/platform/cocoa/platform_cocoa.mm
    )
    target_link_libraries(pebble_platform PUBLIC
        pebble_core
        "-framework Cocoa"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
    )
elseif(PEBBLE_PLATFORM_WINDOWS)
    add_library(pebble_platform STATIC
        src/platform/win32/window_win32.cpp
        src/platform/win32/platform_win32.cpp
    )
    target_link_libraries(pebble_platform PUBLIC
        pebble_core
        opengl32
        user32
        gdi32
    )
else()
    # Stub for unsupported platforms during early development
    add_library(pebble_platform STATIC
        src/platform/platform_stub.cpp
    )
    target_link_libraries(pebble_platform PUBLIC pebble_core)
endif()

# --- Graphics library ---
set(PEBBLE_GFX_SOURCES
    src/gfx/sprite_batch.cpp
    src/gfx/camera.cpp
)

if(PEBBLE_PLATFORM_MACOS)
    list(APPEND PEBBLE_GFX_SOURCES
        src/gfx/metal/renderer_metal.mm
    )
endif()

add_library(pebble_gfx STATIC ${PEBBLE_GFX_SOURCES})
target_include_directories(pebble_gfx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pebble_gfx PUBLIC pebble_platform)

# --- Framework library (tile map, flow field, animation, particles, UI) ---
add_library(pebble_framework STATIC
    src/framework/tile_map.cpp
    src/framework/flow_field.cpp
    src/framework/ui.cpp
    src/framework/particle.cpp
)

target_link_libraries(pebble_framework PUBLIC pebble_gfx)

# --- Convenience target: full engine ---
add_library(pebble INTERFACE)
target_link_libraries(pebble INTERFACE
    pebble_core
    pebble_platform
    pebble_gfx
    pebble_framework
)
