cmake_minimum_required(VERSION 3.20)

project(realtime_session_server
    VERSION 0.1.0
    DESCRIPTION "Linux epoll-based real-time session server in modern C++"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RSS_BUILD_TESTS "Build unit tests" ON)
option(RSS_BUILD_NETWORK_TARGETS "Build Linux epoll server and clients" ON)

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

    add_executable(PacketCodecTest test/PacketCodecTest.cpp)
    target_link_libraries(PacketCodecTest PRIVATE rss_core)
    add_test(NAME PacketCodecTest COMMAND PacketCodecTest)

    add_executable(RoomServiceTest test/RoomServiceTest.cpp)
    target_link_libraries(RoomServiceTest PRIVATE rss_core)
    add_test(NAME RoomServiceTest COMMAND RoomServiceTest)

    add_executable(MessageRouterTest test/MessageRouterTest.cpp)
    target_link_libraries(MessageRouterTest PRIVATE rss_core)
    add_test(NAME MessageRouterTest COMMAND MessageRouterTest)

    add_executable(LatencyStatsTest test/LatencyStatsTest.cpp)
    target_link_libraries(LatencyStatsTest PRIVATE rss_core)
    add_test(NAME LatencyStatsTest COMMAND LatencyStatsTest)

    add_executable(WorkerPoolNotificationTest
        test/WorkerPoolNotificationTest.cpp
        src/net/WorkerPool.cpp
    )
    target_link_libraries(WorkerPoolNotificationTest PRIVATE rss_core Threads::Threads)
    add_test(NAME WorkerPoolNotificationTest COMMAND WorkerPoolNotificationTest)

    if(RSS_BUILD_NETWORK_TARGETS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(EventFdCompletionNotifierTest
            test/EventFdCompletionNotifierTest.cpp
            src/net/EventFdCompletionNotifier.cpp
        )
        target_include_directories(EventFdCompletionNotifierTest PRIVATE ${PROJECT_SOURCE_DIR}/include)
        add_test(NAME EventFdCompletionNotifierTest COMMAND EventFdCompletionNotifierTest)
    endif()
endif()
