add_executable(rss_server
    src/main.cpp
    src/ShutdownSignalMonitor.cpp
)
set_target_properties(rss_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_link_libraries(rss_server PRIVATE rss_server_net)
rss_enable_project_warnings(rss_server)
