add_executable(webui
    src/main.cpp
)

include(GNUInstallDirs)

target_link_libraries(webui PRIVATE
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
)

target_include_directories(webui PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

install(TARGETS webui
    RUNTIME DESTINATION bin
)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    # pkgconf would prefix the path with the sysroot when cross compiling
    set(SAVED_PKG_CONFIG_SYSROOT_DIR "$ENV{PKG_CONFIG_SYSROOT_DIR}")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
    pkg_get_variable(SYSTEMD_SYSTEM_UNIT_DIR systemd systemdsystemunitdir)
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${SAVED_PKG_CONFIG_SYSROOT_DIR}")
endif()

if(NOT SYSTEMD_SYSTEM_UNIT_DIR)
    set(SYSTEMD_SYSTEM_UNIT_DIR "/lib/systemd/system")
endif()

set(EVEREST_UI_SYSTEMD_UNIT_DIR "${SYSTEMD_SYSTEM_UNIT_DIR}"
    CACHE PATH "systemd system unit install directory"
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/systemd/webui.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/webui.service"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/webui.service"
    DESTINATION "${EVEREST_UI_SYSTEMD_UNIT_DIR}"
)
