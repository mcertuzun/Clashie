# MiniClash Game

add_executable(miniclash
    src/main.cpp
    src/simulation.cpp
    src/replay.cpp
    src/building_data.cpp
    src/troop_data.cpp
)

target_include_directories(miniclash PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(miniclash PRIVATE pebble)

if(PEBBLE_PLATFORM_WEB)
    # Embed data files into the WASM virtual filesystem
    target_link_options(miniclash PRIVATE
        "--preload-file" "${CMAKE_SOURCE_DIR}/data@/data"
    )
    # Set the output name so the .js/.wasm match the HTML shell's <script> tag
    set_target_properties(miniclash PROPERTIES OUTPUT_NAME "miniclash")
else()
    # Copy data files to build directory
    add_custom_command(TARGET miniclash POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:miniclash>/data
        COMMENT "Copying data files to build directory"
    )
endif()
