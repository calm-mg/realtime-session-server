add_executable(rss_server src/main.cpp)
target_link_libraries(rss_server PRIVATE rss_server_net)
rss_enable_project_warnings(rss_server)
