find_package(yaml-cpp REQUIRED)
find_package(${QT_PACKAGE_NAME} COMPONENTS Core Network)

add_executable(everest-ui-api
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
    modules/FactoryResetRuntime.cpp
    modules/SystemLogs.cpp
    modules/NetworkConfiguration.cpp
    modules/SSHConfiguration.cpp
    libs/ConsoleConnector.cpp
    libs/RebootCommand.cpp
    libs/RebootSettings.cpp
    libs/BackendConfig.cpp
    libs/NetworkInterfaceUtils.cpp
    libs/ResponseBuilder.cpp
    libs/SystemdService.cpp
    libs/EverestServiceControl.cpp
    libs/YamlUtils.cpp
)

target_compile_definitions(everest-ui-api PRIVATE
    $<$<BOOL:${EVEREST_UI_RPC_DEBUG}>:EVEREST_UI_RPC_DEBUG>
)

target_link_libraries(everest-ui-api
    everest_ui_config
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::WebSockets
    ${QT_PACKAGE_NAME}::DBus
    yaml-cpp
)

target_include_directories(everest-ui-api PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs"
)

install(TARGETS everest-ui-api
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/everest-ui"
)

configure_file(config/backend.conf "${CMAKE_CURRENT_BINARY_DIR}/backend.conf" COPYONLY)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/backend.conf"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/everest-ui/config"
)
