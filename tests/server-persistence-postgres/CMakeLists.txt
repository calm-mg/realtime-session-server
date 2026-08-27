add_executable(rss_postgres_persistence_tests
    PostgresUserRepositoryTest.cpp
)

target_link_libraries(rss_postgres_persistence_tests
    PRIVATE
        rss_server_persistence_postgres
        GTest::gtest_main
)

gtest_discover_tests(
    rss_postgres_persistence_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30
)
