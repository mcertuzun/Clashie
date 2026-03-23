# MiniClash Game

add_executable(miniclash
    src/main.cpp
)

target_include_directories(miniclash PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(miniclash PRIVATE pebble)

# Copy data files to build directory
add_custom_command(TARGET miniclash POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:miniclash>/data
    COMMENT "Copying data files to build directory"
)
