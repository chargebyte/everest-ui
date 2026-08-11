find_package(${QT_PACKAGE_NAME} REQUIRED COMPONENTS Test)

add_executable(pcap_test
    PCAPTest.cpp
    ../backend/api/modules/PCAP.cpp
    ../backend/api/libs/ConsoleConnector.cpp
    ../backend/api/libs/BackendConfig.cpp
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
