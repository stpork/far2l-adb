cmake_minimum_required(VERSION 3.10)
project(ADB_Basic)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(src)
include_directories(../far2l/far2sdk)
include_directories(../utils/include)
include_directories(../WinPort)

# Basic source files - only what we need
set(SOURCES
    src/FARPlugin.cpp
    src/ADBPlugin.cpp
    src/ADBShell.cpp
    src/ADBDevice.cpp
)

# Create the basic plugin
add_library(adb SHARED ${SOURCES})

# Link with required libraries
target_link_libraries(adb WinPort)

# Add compile definitions
target_compile_definitions(adb PRIVATE
    UNICODE
    _UNICODE
    FAR2L_PLUGIN
)

# Set output name
set_target_properties(adb PROPERTIES
    OUTPUT_NAME "adb"
    SUFFIX ".far-plug-wide"
    PREFIX ""
)

# Install the plugin to the far2l plugins directory
install(TARGETS adb
    DESTINATION ${CMAKE_SOURCE_DIR}/_build/install/far2l.app/Contents/MacOS/Plugins/adb/plug
)

# Make the adb target also install the plugin
add_custom_command(TARGET adb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:adb> ${CMAKE_SOURCE_DIR}/_build/install/far2l.app/Contents/MacOS/Plugins/adb/plug/
    COMMENT "Installing ADB plugin to far2l"
)
