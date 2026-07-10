add_executable(webserver
    main.cpp
    auth/AuthManager.cpp
    auth/AuthManager.hpp
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
    ui/AppTitleResolver.cpp
    ui/AppTitleResolver.hpp
    ui/UiOccupancyTracker.cpp
    ui/UiOccupancyTracker.hpp
    ws/WebSocketProxySession.cpp
    ws/WebSocketProxySession.hpp
)

target_link_libraries(webserver PRIVATE
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
    ${QT_PACKAGE_NAME}::WebSockets
)

target_include_directories(webserver PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/auth"
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/http"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/ws"
)

set(WEBSERVER_INSTALL_ROOT "${EVEREST_UI_INSTALL_PUBLIC_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config/frontend.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/frontend.conf"
    @ONLY
)

install(TARGETS webserver
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/everest-ui"
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/frontend.conf"
    DESTINATION "/etc/everest-ui"
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/public/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/everest-ui/public"
)
