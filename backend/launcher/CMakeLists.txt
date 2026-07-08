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

set(EVEREST_UI_SYSTEMD_UNIT_DIR "/lib/systemd/system"
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
