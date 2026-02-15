# MTP Plugin for far2l

# Find required packages using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMTP REQUIRED libmtp)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

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
target_include_directories(mtp PRIVATE ${LIBMTP_INCLUDE_DIRS})

# Link with libmtp and libusb (static linking)
# On macOS, need to link statically and add frameworks
if(APPLE)
    # Find static library paths
    find_library(LIBMTP_STATIC libmtp.a PATHS ${LIBMTP_LIBRARY_DIRS})
    find_library(LIBUSB_STATIC libusb-1.0.a PATHS ${LIBUSB_LIBRARY_DIRS})

    if(NOT LIBMTP_STATIC)
        message(FATAL_ERROR "Could not find libmtp.a for static linking")
    endif()
    if(NOT LIBUSB_STATIC)
        message(FATAL_ERROR "Could not find libusb-1.0.a for static linking")
    endif()

    target_link_libraries(mtp
        ${LIBMTP_STATIC}
        ${LIBUSB_STATIC}
        -liconv
    )

    # Add macOS frameworks required by libusb
    target_link_libraries(mtp
        "-framework CoreFoundation"
        "-framework IOKit"
        "-framework Security"
    )
else()
    # On Linux, use dynamic linking
    target_link_libraries(mtp ${LIBMTP_LIBRARIES} ${LIBUSB_LIBRARIES})
    target_link_directories(mtp PRIVATE ${LIBMTP_LIBRARY_DIRS} ${LIBUSB_LIBRARY_DIRS})
endif()

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
