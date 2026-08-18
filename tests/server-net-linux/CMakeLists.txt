add_executable(rss_server_net_tests
    EpollEventLoopTest.cpp
    EmbeddedServerTest.cpp
    EventFdCompletionNotifierTest.cpp
    TcpServerBackpressureTest.cpp
)
target_link_libraries(rss_server_net_tests
    PRIVATE
        rss_load_scenario_runtime
        rss_server_net
        GTest::gtest_main
)
gtest_discover_tests(
    rss_server_net_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30
)
