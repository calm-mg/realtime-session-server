add_executable(rss_server_core_tests
    BoundedBlockingQueueTest.cpp
    MessageRouterTest.cpp
    OverloadStatsTest.cpp
    ReadBackpressureControllerTest.cpp
    RoomServiceTest.cpp
    ServerConfigTest.cpp
    SessionTest.cpp
    WorkerPoolNotificationTest.cpp
)
target_link_libraries(rss_server_core_tests
    PRIVATE
        rss_server_core
        GTest::gtest_main
)
gtest_discover_tests(
    rss_server_core_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 15
)
