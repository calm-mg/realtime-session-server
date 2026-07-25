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

include(cmake/Dependencies.cmake)

find_package(Threads REQUIRED)

add_library(rss_core
    src/protocol/PacketCodec.cpp
    src/protocol/PacketTypes.cpp
    src/domain/User.cpp
    src/domain/Room.cpp
    src/domain/Lobby.cpp
    src/service/RoomService.cpp
    src/service/MessageRouter.cpp
)

target_include_directories(rss_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(rss_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(rss_core PRIVATE /W4)
else()
    target_compile_options(rss_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RSS_BUILD_NETWORK_TARGETS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(rss_net
        src/net/EventFdCompletionNotifier.cpp
        src/net/EpollEventLoop.cpp
        src/net/Session.cpp
        src/net/TcpServer.cpp
        src/net/WorkerPool.cpp
    )

    target_link_libraries(rss_net
        PUBLIC
            rss_core
            Threads::Threads
    )

    target_compile_features(rss_net PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options(rss_net PRIVATE /W4)
    else()
        target_compile_options(rss_net PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(rss_server src/main.cpp)
    target_link_libraries(rss_server PRIVATE rss_net)

    add_executable(rss_console_client client/console_client.cpp)
    target_link_libraries(rss_console_client PRIVATE rss_core Threads::Threads)

    add_executable(rss_load_test_client tools/load_test_client.cpp)
    target_link_libraries(rss_load_test_client PRIVATE rss_core Threads::Threads)
else()
    message(STATUS "Linux-only epoll server/client targets are disabled on this platform.")
endif()

if(RSS_BUILD_TESTS)
    enable_testing()
    rss_enable_googletest()
    include(GoogleTest)

    add_executable(rss_core_tests
        src/net/Session.cpp
        src/net/WorkerPool.cpp
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
            rss_core
            Threads::Threads
            GTest::gtest_main
    )
    gtest_discover_tests(rss_core_tests DISCOVERY_MODE PRE_TEST)

    if(RSS_BUILD_NETWORK_TARGETS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(rss_net_tests
            test/EventFdCompletionNotifierTest.cpp
            test/TcpServerBackpressureTest.cpp
        )
        target_link_libraries(rss_net_tests PRIVATE rss_net GTest::gtest_main)
        gtest_discover_tests(rss_net_tests DISCOVERY_MODE PRE_TEST)
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
            rss_core
            benchmark::benchmark_main
    )
endif()

include(cmake/CodeQuality.cmake)
