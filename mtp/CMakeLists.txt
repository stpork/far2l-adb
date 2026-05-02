# MTP/PTP plugin module backed by libmtp.

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMTP REQUIRED IMPORTED_TARGET libmtp)
# libusb is used directly to issue a SET_CONFIGURATION control transfer
# that dislodges any pre-existing interface claim (macOS icdd, Linux
# gvfs-mtp) before libmtp opens the device. It's also a transitive dep
# of libmtp, but we link explicitly to use its public API.
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

set(SOURCES
    src/FARPlugin.cpp
    src/MTPPlugin.cpp
    src/MTPDialogs.cpp
    src/MTPLog.cpp
    src/LibMtpBackend.cpp
)

add_library(mtp MODULE ${SOURCES})

target_link_libraries(mtp utils far2l PkgConfig::LIBMTP PkgConfig::LIBUSB)

if(APPLE)
    # CoreFoundation: CFPreferences{SetValue,Synchronize} for the
    # programmatic Image Capture "No application" pref write that lets
    # the plugin coexist with icdd without manual GUI setup.
    # libproc (proc_listallpids / proc_pidpath) is part of libSystem
    # so it's already linked transitively.
    target_link_libraries(mtp "-framework CoreFoundation")
endif()

target_compile_definitions(mtp PRIVATE
    -DWINPORT_DIRECT
    -DUNICODE
    -D_UNICODE
    -DFAR_DONT_USE_INTERNALS
    # Activate MTPLog's DBG() in Debug builds — the macro is gated on
    # DEBUG/_DEBUG (matches ADBLog), and CMake's Debug config doesn't
    # set either of them automatically on Unix.
    "$<$<CONFIG:Debug>:DEBUG>"
)

target_include_directories(mtp PRIVATE .)
target_include_directories(mtp PRIVATE ../far2l/far2sdk)
target_include_directories(mtp PRIVATE ../WinPort)

set_target_properties(mtp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${INSTALL_DIR}/Plugins/mtp/plug"
    PREFIX ""
    SUFFIX ".far-plug-wide"
)
