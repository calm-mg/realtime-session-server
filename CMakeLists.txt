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
option(RSS_BUILD_QT_CLIENT "Build the cross-platform Qt desktop client" OFF)

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

if(RSS_BUILD_QT_CLIENT)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Network Widgets)
    if(RSS_BUILD_TESTS)
        find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    endif()
    qt_standard_project_setup()
    add_subdirectory(apps/qt-client)
endif()

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
    add_subdirectory(tests)
endif()

if(RSS_BUILD_BENCHMARKS)
    rss_enable_google_benchmark()
    add_subdirectory(benchmarks)
endif()

include(cmake/CodeQuality.cmake)
