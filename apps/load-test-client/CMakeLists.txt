add_executable(rss_load_test_client src/main.cpp)
target_link_libraries(rss_load_test_client
    PRIVATE
        rss_protocol
        rss_load_test_support
        Threads::Threads
)
rss_enable_project_warnings(rss_load_test_client)
