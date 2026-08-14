cmake_minimum_required(VERSION 3.20)

project(realtime_session_server
    VERSION 0.1.0
    DESCRIPTION "Linux epoll-based real-time session server in modern C++"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RSS_BUILD_TESTS "Build unit tests" ON)
option(RSS_BUILD_NETWORK_TARGETS "Build Linux epoll server and clients" ON)
option(RSS_BUILD_BENCHMARKS "Build microbenchmarks" OFF)

function(rss_enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

include(cmake/Dependencies.cmake)

find_package(Threads REQUIRED)

add_subdirectory(libs/protocol)
add_subdirectory(libs/server-core)
add_subdirectory(libs/load-test-support)

if(RSS_BUILD_NETWORK_TARGETS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(libs/server-net-linux)
    add_subdirectory(apps/server)
    add_subdirectory(apps/console-client)
    add_subdirectory(apps/load-test-client)
else()
    message(STATUS "Linux-only epoll server/client targets are disabled on this platform.")
endif()

if(RSS_BUILD_TESTS)
    enable_testing()
    rss_enable_googletest()
    include(GoogleTest)

    add_executable(rss_core_tests
        test/LatencyStatsTest.cpp
        test/MessageRouterTest.cpp
        test/PacketCodecTest.cpp
        test/RoomServiceTest.cpp
        test/BoundedBlockingQueueTest.cpp
        test/OverloadStatsTest.cpp
        test/ReadBackpressureControllerTest.cpp
        test/ServerConfigTest.cpp
        test/SessionTest.cpp
        test/WorkerPoolNotificationTest.cpp
    )
    target_link_libraries(
        rss_core_tests
        PRIVATE
            rss_server_core
            rss_load_test_support
            GTest::gtest_main
    )
    gtest_discover_tests(
        rss_core_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 15
    )

    if(RSS_BUILD_NETWORK_TARGETS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(rss_net_tests
            test/EpollEventLoopTest.cpp
            test/EventFdCompletionNotifierTest.cpp
            test/TcpServerBackpressureTest.cpp
        )
        target_link_libraries(rss_net_tests PRIVATE rss_server_net GTest::gtest_main)
        gtest_discover_tests(
            rss_net_tests
            DISCOVERY_MODE PRE_TEST
            PROPERTIES TIMEOUT 30
        )
    endif()
endif()

if(RSS_BUILD_BENCHMARKS)
    rss_enable_google_benchmark()

    add_executable(rss_microbenchmarks
        benchmark/LatencyStatsBenchmark.cpp
        benchmark/MessageRouterBenchmark.cpp
        benchmark/PacketCodecBenchmark.cpp
    )
    target_link_libraries(
        rss_microbenchmarks
        PRIVATE
            rss_protocol
            rss_server_core
            rss_load_test_support
            benchmark::benchmark_main
    )
endif()

include(cmake/CodeQuality.cmake)
