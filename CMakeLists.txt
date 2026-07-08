cmake_minimum_required(VERSION 3.20)

project(everest_ui LANGUAGES CXX)

include(GNUInstallDirs)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX
        "${CMAKE_SOURCE_DIR}/install"
        CACHE PATH "Install path prefix" FORCE
    )
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(EVEREST_UI_INSTALL_CONFIG_DIR
    "/etc/everest-ui")
set(EVEREST_UI_INSTALL_BACKEND_CONFIG
    "${EVEREST_UI_INSTALL_CONFIG_DIR}/backend.conf")
set(EVEREST_UI_INSTALL_FRONTEND_CONFIG
    "${EVEREST_UI_INSTALL_CONFIG_DIR}/frontend.conf")
set(EVEREST_UI_INSTALL_LIBEXECDIR
    "${CMAKE_INSTALL_FULL_LIBEXECDIR}/everest-ui")
set(EVEREST_UI_INSTALL_API_BINARY
    "${EVEREST_UI_INSTALL_LIBEXECDIR}/api")
set(EVEREST_UI_INSTALL_WEBSERVER_BINARY
    "${EVEREST_UI_INSTALL_LIBEXECDIR}/webserver")
set(EVEREST_UI_INSTALL_PUBLIC_DIR
    "${CMAKE_INSTALL_FULL_DATADIR}/everest-ui/public")
set(EVEREST_UI_INSTALL_AUTH_FILE
    "/var/lib/everest/everest-ui/auth.json")

configure_file(
    "${CMAKE_SOURCE_DIR}/backend/InstallPaths.hpp.in"
    "${CMAKE_BINARY_DIR}/InstallPaths.hpp"
    @ONLY
)

set(EVEREST_UI_QT_VERSION "AUTO" CACHE STRING "Qt version to use: AUTO, 5, or 6")
set_property(CACHE EVEREST_UI_QT_VERSION PROPERTY STRINGS AUTO 5 6)

if(EVEREST_UI_QT_VERSION STREQUAL "6")
    find_package(Qt6 REQUIRED COMPONENTS Core Network WebSockets DBus)
    set(QT_PACKAGE_NAME Qt6)
elseif(EVEREST_UI_QT_VERSION STREQUAL "5")
    find_package(Qt5 REQUIRED COMPONENTS Core Network WebSockets DBus)
    set(QT_PACKAGE_NAME Qt5)
elseif(EVEREST_UI_QT_VERSION STREQUAL "AUTO")
    find_package(Qt6 COMPONENTS Core Network WebSockets DBus)
    find_package(Qt5 COMPONENTS Core Network WebSockets DBus)

    if(Qt6_FOUND)
        set(QT_PACKAGE_NAME Qt6)
    elseif(Qt5_FOUND)
        set(QT_PACKAGE_NAME Qt5)
    else()
        message(FATAL_ERROR
            "Neither Qt6 nor Qt5 with components Core, Network, WebSockets, and DBus was found"
        )
    endif()
else()
    message(FATAL_ERROR
        "EVEREST_UI_QT_VERSION must be one of: AUTO, 5, 6"
    )
endif()

add_subdirectory(backend)
