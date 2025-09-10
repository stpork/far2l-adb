# MTP Plugin for far2l

# Basic source files
set(SOURCES
    src/FARPlugin.cpp
    src/MTPPlugin.cpp
    src/MTPDevice.cpp
    src/MTPFileSystem.cpp
    src/MTPLog.cpp
    src/MTPDialogs.cpp
)

# Create the plugin
add_library(mtp MODULE ${SOURCES})

# Link with required libraries
target_link_libraries(mtp utils far2l)

# Add libmtp static library
target_link_libraries(mtp 
    /opt/homebrew/lib/libmtp.a
    /opt/homebrew/Cellar/libusb/1.0.29/lib/libusb-1.0.a
    -liconv
)

# Add macOS frameworks
target_link_libraries(mtp 
    "-F/System/Library/Frameworks"
    "-framework CoreFoundation"
    "-framework IOKit" 
    "-framework Security"
)

# Add compile definitions
target_compile_definitions(mtp PRIVATE
    -DWINPORT_DIRECT
    -DUNICODE
    -D_UNICODE
    -DFAR_DONT_USE_INTERNALS
)

# Add include directories
target_include_directories(mtp PRIVATE .)
target_include_directories(mtp PRIVATE ../far2l/far2sdk)
target_include_directories(mtp PRIVATE ../WinPort)
# target_include_directories(mtp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libmtp/src)
target_include_directories(mtp PRIVATE /opt/homebrew/Cellar/libmtp/1.1.22/include)

# Set output properties
set_target_properties(mtp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${INSTALL_DIR}/Plugins/mtp/plug"
    PREFIX ""
    SUFFIX ".far-plug-wide"
)

# Copy resource files
set(CURRENT_TARGET "mtp")
file(GLOB_RECURSE RESOURCE_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/configs
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/*"
)
# No auxiliary files for MTP plugin yet
