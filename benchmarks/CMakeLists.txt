add_executable(rss_microbenchmarks
    LatencyStatsBenchmark.cpp
    MessageRouterBenchmark.cpp
    PacketCodecBenchmark.cpp
)

set_target_properties(rss_microbenchmarks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_link_libraries(rss_microbenchmarks
    PRIVATE
        rss_protocol
        rss_server_core
        rss_load_test_support
        benchmark::benchmark_main
)

rss_enable_project_warnings(rss_microbenchmarks)
