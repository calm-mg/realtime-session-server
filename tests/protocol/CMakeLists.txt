add_executable(rss_protocol_tests
    PacketCodecTest.cpp
    StructuredPayloadTest.cpp
    TextValidationTest.cpp
)
target_link_libraries(rss_protocol_tests PRIVATE rss_protocol GTest::gtest_main)
gtest_discover_tests(
    rss_protocol_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 15
)
