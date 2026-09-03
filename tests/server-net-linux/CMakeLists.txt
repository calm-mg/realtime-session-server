add_executable(rss_server_net_tests
    EpollEventLoopTest.cpp
    EmbeddedServerTest.cpp
    EnvironmentInfoTest.cpp
    EventFdCompletionNotifierTest.cpp
    ProgramTest.cpp
    PostgresServerLoginTest.cpp
    ScenarioClientTest.cpp
    ScenarioRunnerTest.cpp
    ServerProgramTest.cpp
    ServerSignalTest.cpp
    TcpServerBackpressureTest.cpp
)
add_dependencies(rss_server_net_tests rss_server)
target_compile_definitions(rss_server_net_tests
    PRIVATE
        RSS_SERVER_EXECUTABLE_PATH="$<TARGET_FILE:rss_server>"
)
target_include_directories(rss_server_net_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/load-scenario-runner/src"
)
target_link_libraries(rss_server_net_tests
    PRIVATE
        rss_load_test_support
        rss_load_scenario_runtime
        rss_server_net
        GTest::gtest_main
)
gtest_discover_tests(
    rss_server_net_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30
)
