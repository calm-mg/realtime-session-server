add_executable(rss_load_test_support_tests LatencyStatsTest.cpp)
target_link_libraries(rss_load_test_support_tests
    PRIVATE
        rss_load_test_support
        GTest::gtest_main
)
gtest_discover_tests(
    rss_load_test_support_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 15
)
