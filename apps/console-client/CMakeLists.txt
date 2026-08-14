add_executable(rss_console_client src/main.cpp)
set_target_properties(rss_console_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_link_libraries(rss_console_client PRIVATE rss_protocol Threads::Threads)
rss_enable_project_warnings(rss_console_client)
