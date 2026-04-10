cmake_minimum_required(VERSION 3.20)

project(everest_ui LANGUAGES CXX)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX
        "${CMAKE_SOURCE_DIR}/install"
        CACHE PATH "Install path prefix" FORCE
    )
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(EVEREST_UI_QT_VERSION "AUTO" CACHE STRING "Qt version to use: AUTO, 5, or 6")
set_property(CACHE EVEREST_UI_QT_VERSION PROPERTY STRINGS AUTO 5 6)

if(EVEREST_UI_QT_VERSION STREQUAL "6")
    find_package(Qt6 REQUIRED COMPONENTS Core Network WebSockets DBus)
    set(QT_PACKAGE_NAME Qt6)
elseif(EVEREST_UI_QT_VERSION STREQUAL "5")
    find_package(Qt5 REQUIRED COMPONENTS Core Network WebSockets DBus)
    set(QT_PACKAGE_NAME Qt5)
elseif(EVEREST_UI_QT_VERSION STREQUAL "AUTO")
    find_package(Qt6 QUIET COMPONENTS Core Network WebSockets DBus)

    if(Qt6_FOUND)
        set(QT_PACKAGE_NAME Qt6)
    else()
        find_package(Qt5 REQUIRED COMPONENTS Core Network WebSockets DBus)
        set(QT_PACKAGE_NAME Qt5)
    endif()
else()
    message(FATAL_ERROR
        "EVEREST_UI_QT_VERSION must be one of: AUTO, 5, 6"
    )
endif()

add_subdirectory(backend)
