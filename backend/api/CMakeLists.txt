find_package(yaml-cpp REQUIRED)

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
    modules/Logs.cpp
    libs/ConsoleConnector.cpp
    libs/BackendConfig.cpp
    libs/ResponseBuilder.cpp
    libs/SystemdService.cpp
    libs/YamlUtils.cpp
)

target_link_libraries(api
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::WebSockets
    ${QT_PACKAGE_NAME}::DBus
    yaml-cpp
)

target_include_directories(api PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs"
)

install(TARGETS api
    RUNTIME DESTINATION bin
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/backend.conf"
    DESTINATION config
)
