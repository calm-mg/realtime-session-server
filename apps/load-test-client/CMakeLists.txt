add_executable(rss_load_test_client src/main.cpp)
set_target_properties(rss_load_test_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_link_libraries(rss_load_test_client
    PRIVATE
        rss_protocol
        rss_load_test_support
        Threads::Threads
)
rss_enable_project_warnings(rss_load_test_client)
