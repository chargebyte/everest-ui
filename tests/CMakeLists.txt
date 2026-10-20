find_package(${QT_PACKAGE_NAME} REQUIRED COMPONENTS Test)

add_executable(factory_reset_test FactoryResetTest.cpp
    ../backend/api/modules/FactoryResetRuntime.cpp ../backend/api/libs/ConsoleConnector.cpp)
target_include_directories(factory_reset_test PRIVATE ../backend/api ../backend/api/modules ../backend/api/libs)
target_link_libraries(factory_reset_test PRIVATE ${QT_PACKAGE_NAME}::Core ${QT_PACKAGE_NAME}::Test)
add_test(NAME factory_reset_test COMMAND factory_reset_test)

add_executable(layered_config_test
    LayeredConfigTest.cpp
    ../backend/webserver/config/ServerConfig.cpp
    ../backend/webserver/http/RequestSecurity.cpp
)
target_include_directories(layered_config_test PRIVATE
    "${CMAKE_SOURCE_DIR}/backend/webserver/config"
    "${CMAKE_SOURCE_DIR}/backend/webserver/http"
)
target_link_libraries(layered_config_test PRIVATE everest_ui_config ${QT_PACKAGE_NAME}::Network ${QT_PACKAGE_NAME}::Test)
add_test(NAME layered_config_test COMMAND layered_config_test)

add_test(NAME config_overlay_install_test COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}" "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/ConfigOverlayInstallTest.cmake")

add_executable(request_parsing_test
    RequestParsingTest.cpp
    ../backend/webserver/http/RequestParsing.cpp
    ../backend/webserver/http/StaticResponse.cpp
)
target_include_directories(request_parsing_test PRIVATE ../backend/webserver/http)
target_link_libraries(request_parsing_test PRIVATE
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Test
)
add_test(NAME request_parsing_test COMMAND request_parsing_test)

add_executable(request_security_test
    RequestSecurityTest.cpp
    ../backend/webserver/http/RequestSecurity.cpp
)
target_include_directories(request_security_test PRIVATE ../backend/webserver/http)
target_link_libraries(request_security_test PRIVATE
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::Test
)
add_test(NAME request_security_test COMMAND request_security_test)

add_executable(pcap_test
    PCAPTest.cpp
    ../backend/api/modules/PCAP.cpp
    ../backend/api/libs/ConsoleConnector.cpp
    ../backend/api/libs/BackendConfig.cpp
    ../backend/api/libs/NetworkInterfaceUtils.cpp
    ../backend/api/libs/ResponseBuilder.cpp
)

target_include_directories(pcap_test PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/backend/api"
    "${CMAKE_SOURCE_DIR}/backend/api/modules"
    "${CMAKE_SOURCE_DIR}/backend/api/libs"
)

target_link_libraries(pcap_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::Test
)

add_test(NAME pcap_test COMMAND pcap_test)

add_executable(network_interface_utils_test
    NetworkInterfaceUtilsTest.cpp
    ../backend/api/libs/NetworkInterfaceUtils.cpp
    ../backend/api/libs/BackendConfig.cpp
)

target_include_directories(network_interface_utils_test PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/backend/api"
    "${CMAKE_SOURCE_DIR}/backend/api/libs"
)

target_link_libraries(network_interface_utils_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::Test
)

add_test(NAME network_interface_utils_test COMMAND network_interface_utils_test)

add_executable(backend_config_test
    BackendConfigTest.cpp
    ../backend/api/libs/BackendConfig.cpp
)

target_include_directories(backend_config_test PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/backend/api/libs"
)

target_link_libraries(backend_config_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Test
)

add_test(NAME backend_config_test COMMAND backend_config_test)

add_executable(request_handler_test
    RequestHandlerTest.cpp
    ../backend/api/RequestHandler.cpp
    ../backend/api/libs/ResponseBuilder.cpp
)

target_include_directories(request_handler_test PRIVATE
    "${CMAKE_SOURCE_DIR}/backend/api"
    "${CMAKE_SOURCE_DIR}/backend/api/libs"
)

target_link_libraries(request_handler_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::WebSockets
    ${QT_PACKAGE_NAME}::Test
)

add_test(NAME request_handler_test COMMAND request_handler_test)

add_executable(network_configuration_test
    NetworkConfigurationTest.cpp
    ../backend/api/libs/BackendConfig.cpp
    ../backend/api/libs/NetworkInterfaceUtils.cpp
)

target_include_directories(network_configuration_test PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/backend/api"
    "${CMAKE_SOURCE_DIR}/backend/api/modules"
    "${CMAKE_SOURCE_DIR}/backend/api/libs"
)

target_link_libraries(network_configuration_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::Test
)

add_test(NAME network_configuration_test COMMAND network_configuration_test)

add_executable(ssh_configuration_test
    SSHConfigurationTest.cpp
    ../backend/api/modules/SSHConfiguration.cpp
    ../backend/api/libs/BackendConfig.cpp
    ../backend/api/libs/SystemdService.cpp
)

target_include_directories(ssh_configuration_test PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/backend/api"
    "${CMAKE_SOURCE_DIR}/backend/api/modules"
    "${CMAKE_SOURCE_DIR}/backend/api/libs"
)

target_link_libraries(ssh_configuration_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::DBus
    ${QT_PACKAGE_NAME}::Test
)

add_test(NAME ssh_configuration_test COMMAND ssh_configuration_test)

add_executable(everest_config_test
    EverestConfigTest.cpp
    ../backend/api/RpcApiClient.cpp
    ../backend/api/libs/BackendConfig.cpp
    ../backend/api/libs/EverestServiceControl.cpp
    ../backend/api/libs/ResponseBuilder.cpp
    ../backend/api/libs/SystemdService.cpp
    ../backend/api/libs/YamlUtils.cpp
)

target_include_directories(everest_config_test PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/backend/api"
    "${CMAKE_SOURCE_DIR}/backend/api/modules"
    "${CMAKE_SOURCE_DIR}/backend/api/libs"
)

target_link_libraries(everest_config_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::WebSockets
    ${QT_PACKAGE_NAME}::DBus
    ${QT_PACKAGE_NAME}::Test
    yaml-cpp
)

add_test(NAME everest_config_test COMMAND everest_config_test)

add_executable(safety_controller_test
    SafetyControllerTest.cpp
    ../backend/api/RpcApiClient.cpp
    ../backend/api/libs/BackendConfig.cpp
    ../backend/api/libs/ConsoleConnector.cpp
    ../backend/api/libs/EverestServiceControl.cpp
    ../backend/api/libs/SystemdService.cpp
    ../backend/api/libs/YamlUtils.cpp
)

target_include_directories(safety_controller_test PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/backend/api"
    "${CMAKE_SOURCE_DIR}/backend/api/modules"
    "${CMAKE_SOURCE_DIR}/backend/api/libs"
)

target_link_libraries(safety_controller_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::WebSockets
    ${QT_PACKAGE_NAME}::DBus
    ${QT_PACKAGE_NAME}::Test
    yaml-cpp
)

add_test(NAME safety_controller_test COMMAND safety_controller_test)

add_executable(rpc_api_client_test
    RpcApiClientTest.cpp
    ../backend/api/RpcApiClient.cpp
    ../backend/api/libs/BackendConfig.cpp
    ../backend/api/libs/YamlUtils.cpp
)

target_include_directories(rpc_api_client_test PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/backend/api"
    "${CMAKE_SOURCE_DIR}/backend/api/libs"
)

target_link_libraries(rpc_api_client_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::WebSockets
    ${QT_PACKAGE_NAME}::Test
    yaml-cpp
)

add_test(NAME rpc_api_client_test COMMAND rpc_api_client_test)

add_executable(console_connector_test
    ConsoleConnectorTest.cpp
    ../backend/api/libs/ConsoleConnector.cpp
)

target_link_libraries(console_connector_test
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Test
)

add_test(NAME console_connector_test COMMAND console_connector_test)

foreach(rpc_test_target IN ITEMS rpc_api_client_test safety_controller_test everest_config_test)
    target_compile_definitions(${rpc_test_target} PRIVATE
        $<$<BOOL:${EVEREST_UI_RPC_DEBUG}>:EVEREST_UI_RPC_DEBUG>
    )
endforeach()

foreach(config_test_target IN ITEMS pcap_test network_interface_utils_test backend_config_test
        network_configuration_test ssh_configuration_test everest_config_test safety_controller_test rpc_api_client_test)
    target_link_libraries(${config_test_target} everest_ui_config)
endforeach()

add_executable(password_reset_test
    PasswordResetTest.cpp
    ../backend/webserver/ws/WebSocketProxySession.cpp
    ../backend/webserver/auth/AuthManager.cpp
    ../backend/webserver/auth/PasswordReset.cpp
    ../backend/webserver/config/ServerConfig.cpp
    ../backend/webserver/http/RequestParsing.cpp
    ../backend/webserver/http/RequestSecurity.cpp
    ../backend/webserver/http/StaticServer.cpp
    ../backend/webserver/http/StaticContent.cpp
    ../backend/webserver/http/StaticResponse.cpp
    ../backend/webserver/ui/AppTitleResolver.cpp
    ../backend/webserver/ui/UiOccupancyTracker.cpp
)
target_include_directories(password_reset_test PRIVATE
    ../backend/webserver/auth ../backend/webserver/config
    ../backend/webserver/http ../backend/webserver/ui ../backend/webserver/ws
)
target_link_libraries(password_reset_test PRIVATE
    everest_ui_config
    ${QT_PACKAGE_NAME}::Core ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::WebSockets ${QT_PACKAGE_NAME}::Test
)
add_test(NAME password_reset_test COMMAND password_reset_test)
