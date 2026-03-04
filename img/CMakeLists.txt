project(img)

# Added: Find PkgConfig module to use pkg-config for finding libraries
find_package(PkgConfig REQUIRED)

if(APPLE)
    option(IMG_NATIVE "Use native macOS image decoding and processing" ON)
else()
    set(IMG_NATIVE OFF)
endif()

set(SOURCES
    src/Plugin.cpp
    src/Settings.cpp
    src/Image.cpp
    src/ImageView.cpp
    src/ImageAtFull.cpp
    src/ImageAtQV.cpp
    src/ImgLog.cpp
    src/decoder/ImageDecoder.cpp
    src/decoder/ImageDecoder_common.cpp
)

# Initialize library lists for both branches
set(IMG_NATIVE_LIBS "")
set(IMG_CROSSPLATFORM_LIBS "")

if(APPLE)
    list(APPEND SOURCES src/decoder/ImageDecoder_mac.cpp)
    find_library(IMAGEIO ImageIO REQUIRED)
    find_library(COREGRAPHICS CoreGraphics REQUIRED)
    find_library(COREFOUNDATION CoreFoundation REQUIRED)
    find_library(ACCELERATE Accelerate REQUIRED)
    set(IMG_NATIVE_LIBS ${IMAGEIO} ${COREGRAPHICS} ${COREFOUNDATION} ${ACCELERATE})
    # We don't define IMG_NATIVE anymore as it's runtime choice
    # set(EXTRA_DEFINES ${EXTRA_DEFINES} -DIMG_NATIVE) 
endif()

# Always include cross-platform decoders
list(APPEND SOURCES src/decoder/ImageDecoder_stb.cpp)
# Include the new HEIF decoder source file for compilation
list(APPEND SOURCES src/decoder/ImageDecoder_heif.cpp) 
# Include the WebP decoder source file
list(APPEND SOURCES src/decoder/ImageDecoder_webp.cpp)

# --- Integrate WebP and HEIC support using PkgConfig ---
# Check for libwebp
pkg_check_modules(WEBP REQUIRED libwebp)
# Check for libheif
pkg_check_modules(HEIF REQUIRED libheif)

# Always include include directories BEFORE add_library
include_directories(
    ${WEBP_INCLUDE_DIRS}
    ${HEIF_INCLUDE_DIRS}
    .
    ../far2l/far2sdk
    ../WinPort
)

# Use full paths to libraries if possible
foreach(lib_name ${WEBP_LIBRARIES})
    find_library(LIB_WEBP_${lib_name} NAMES ${lib_name} HINTS ${WEBP_LIBRARY_DIRS})
    list(APPEND IMG_CROSSPLATFORM_LIBS ${LIB_WEBP_${lib_name}})
endforeach()

foreach(lib_name ${HEIF_LIBRARIES})
    find_library(LIB_HEIF_${lib_name} NAMES ${lib_name} HINTS ${HEIF_LIBRARY_DIRS})
    list(APPEND IMG_CROSSPLATFORM_LIBS ${LIB_HEIF_${lib_name}})
endforeach()
# --- End Integration ---

add_library(img MODULE ${SOURCES})

# Link all libraries found for the active branch
target_link_libraries(img utils far2l ${IMG_NATIVE_LIBS} ${IMG_CROSSPLATFORM_LIBS})

target_compile_definitions(img PRIVATE
    -DWINPORT_DIRECT
    -DUNICODE
    -D_UNICODE
    ${EXTRA_DEFINES}
)

set_target_properties(img PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${INSTALL_DIR}/Plugins/img/plug"
    PREFIX ""
    SUFFIX ".far-plug-wide"
)

# Copy resource files (config and help)
set(CURRENT_TARGET "img")
file(GLOB_RECURSE RESOURCE_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/configs
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/*"
)
setup_target_auxiliary_files(${CURRENT_TARGET}
    RESOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/configs
    "${INSTALL_DIR}/Plugins/${CURRENT_TARGET}/"
)
add_dependencies(${CURRENT_TARGET} copy_aux_files_for_${CURRENT_TARGET})
