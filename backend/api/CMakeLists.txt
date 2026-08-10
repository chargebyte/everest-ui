find_package(yaml-cpp REQUIRED)
find_package(${QT_PACKAGE_NAME} COMPONENTS Core Network)

add_executable(api
    server.cpp
    RpcApiClient.cpp
    RequestHandler.cpp
    SystemControlHandler.cpp
    modules/PCAP.cpp
    modules/EverestConfig.cpp
    modules/SafetyController.cpp
    modules/OCPPConfig.cpp
    modules/FirmwareUpdate.cpp
    modules/FirmwareUpdateRuntime.cpp
    modules/SystemLogs.cpp
    libs/ConsoleConnector.cpp
    libs/BackendConfig.cpp
    libs/ResponseBuilder.cpp
    libs/SystemdService.cpp
    libs/EverestServiceControl.cpp
    libs/YamlUtils.cpp
)

target_link_libraries(api
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::WebSockets
    ${QT_PACKAGE_NAME}::DBus
    yaml-cpp
)

target_include_directories(api PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs"
)

install(TARGETS api
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/everest-ui"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/backend.conf"
    DESTINATION "/etc/everest-ui"
)
