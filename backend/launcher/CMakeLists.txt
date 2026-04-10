add_executable(webui
    src/main.cpp
)

target_link_libraries(webui PRIVATE
    ${QT_PACKAGE_NAME}::Core
    ${QT_PACKAGE_NAME}::Network
)

target_include_directories(webui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

install(TARGETS webui
    RUNTIME DESTINATION bin
)
