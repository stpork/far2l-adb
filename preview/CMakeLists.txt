project(preview)

find_package(PkgConfig REQUIRED)

if(APPLE)
    option(PREVIEW_NATIVE "Use native macOS image decoding and processing" ON)
else()
    set(PREVIEW_NATIVE OFF)
endif()

set(SOURCES
    src/Plugin.cpp
    src/Settings.cpp
    src/Image.cpp
    src/ImageView.cpp
    src/ImageAtFull.cpp
    src/ImageAtQV.cpp
    src/PreviewLog.cpp
    src/decoder/ImageDecoder.cpp
    src/decoder/ImageDecoder_common.cpp
    src/decoder/ImageDecoder_stb.cpp
    src/decoder/ImageDecoder_heif.cpp
    src/decoder/ImageDecoder_webp.cpp
    src/decoder/ImageDecoder_tiff.cpp
)

set(PREVIEW_NATIVE_LIBS "")
set(PREVIEW_CROSSPLATFORM_LIBS "")
set(PREVIEW_OPT_DEFS "")
set(PREVIEW_OPT_INCDIRS "")

if(APPLE)
    list(APPEND SOURCES src/decoder/ImageDecoder_mac.cpp)
    find_library(IMAGEIO ImageIO REQUIRED)
    find_library(COREGRAPHICS CoreGraphics REQUIRED)
    find_library(COREFOUNDATION CoreFoundation REQUIRED)
    find_library(ACCELERATE Accelerate REQUIRED)
    set(PREVIEW_NATIVE_LIBS ${IMAGEIO} ${COREGRAPHICS} ${COREFOUNDATION} ${ACCELERATE})
endif()

# Soft-detect optional image libraries — build succeeds even if absent
pkg_check_modules(WEBP QUIET libwebp)
if(WEBP_FOUND)
    list(APPEND PREVIEW_OPT_DEFS -DHAVE_WEBP)
    list(APPEND PREVIEW_OPT_INCDIRS ${WEBP_INCLUDE_DIRS})
    foreach(lib_name ${WEBP_LIBRARIES})
        find_library(LIB_WEBP_${lib_name} NAMES ${lib_name} HINTS ${WEBP_LIBRARY_DIRS})
        if(LIB_WEBP_${lib_name})
            list(APPEND PREVIEW_CROSSPLATFORM_LIBS ${LIB_WEBP_${lib_name}})
        else()
            list(APPEND PREVIEW_CROSSPLATFORM_LIBS ${lib_name})
        endif()
    endforeach()
endif()

pkg_check_modules(HEIF QUIET libheif)
if(HEIF_FOUND)
    list(APPEND PREVIEW_OPT_DEFS -DHAVE_HEIF)
    list(APPEND PREVIEW_OPT_INCDIRS ${HEIF_INCLUDE_DIRS})
    foreach(lib_name ${HEIF_LIBRARIES})
        find_library(LIB_HEIF_${lib_name} NAMES ${lib_name} HINTS ${HEIF_LIBRARY_DIRS})
        if(LIB_HEIF_${lib_name})
            list(APPEND PREVIEW_CROSSPLATFORM_LIBS ${LIB_HEIF_${lib_name}})
        else()
            list(APPEND PREVIEW_CROSSPLATFORM_LIBS ${lib_name})
        endif()
    endforeach()
endif()

pkg_check_modules(TIFF QUIET libtiff-4)
if(NOT TIFF_FOUND)
    pkg_check_modules(TIFF QUIET libtiff)
endif()
if(TIFF_FOUND)
    list(APPEND PREVIEW_OPT_DEFS -DHAVE_TIFF)
    list(APPEND PREVIEW_OPT_INCDIRS ${TIFF_INCLUDE_DIRS})
    foreach(lib_name ${TIFF_LIBRARIES})
        find_library(LIB_TIFF_${lib_name} NAMES ${lib_name} HINTS ${TIFF_LIBRARY_DIRS})
        if(LIB_TIFF_${lib_name})
            list(APPEND PREVIEW_CROSSPLATFORM_LIBS ${LIB_TIFF_${lib_name}})
        else()
            list(APPEND PREVIEW_CROSSPLATFORM_LIBS ${lib_name})
        endif()
    endforeach()
endif()

include_directories(
    ${PREVIEW_OPT_INCDIRS}
    .
    ../far2l/far2sdk
    ../WinPort
)

add_library(preview MODULE ${SOURCES})

target_link_libraries(preview utils far2l ${PREVIEW_NATIVE_LIBS} ${PREVIEW_CROSSPLATFORM_LIBS})

target_compile_definitions(preview PRIVATE
    -DWINPORT_DIRECT
    -DUNICODE
    -D_UNICODE
    ${PREVIEW_OPT_DEFS}
)

set_target_properties(preview PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${INSTALL_DIR}/Plugins/preview/plug"
    PREFIX ""
    SUFFIX ".far-plug-wide"
)

# Copy resource files (config and help)
set(CURRENT_TARGET "preview")
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
