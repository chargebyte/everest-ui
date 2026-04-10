add_executable(webserver
    main.cpp
    config/ServerConfig.cpp
    config/ServerConfig.hpp
    http/RequestParsing.cpp
    http/RequestParsing.hpp
    http/StaticContent.cpp
    http/StaticContent.hpp
    http/StaticResponse.cpp
    http/StaticResponse.hpp
    http/StaticServer.cpp
    http/StaticServer.hpp
    ws/WebSocketProxySession.cpp
    ws/WebSocketProxySession.hpp
)

target_link_libraries(webserver PRIVATE
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::WebSockets
)

target_include_directories(webserver PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/http"
    "${CMAKE_CURRENT_SOURCE_DIR}/ws"
)

set(WEBSERVER_INSTALL_ROOT "../public")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config/frontend.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/frontend.conf"
    @ONLY
)

install(TARGETS webserver
    RUNTIME DESTINATION bin
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/frontend.conf"
    DESTINATION config
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/public/"
    DESTINATION public
)
