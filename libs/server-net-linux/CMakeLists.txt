add_library(rss_server_net
    src/EpollEventLoop.cpp
    src/EventFdCompletionNotifier.cpp
    src/TcpServer.cpp
)

target_include_directories(rss_server_net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rss_server_net
    PUBLIC
        rss_protocol
        rss_server_core
        Threads::Threads
)

target_compile_features(rss_server_net PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_server_net)
