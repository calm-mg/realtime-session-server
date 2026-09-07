add_executable(rss_microbenchmarks
    BenchmarkMain.cpp
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
        benchmark::benchmark
)

rss_enable_project_warnings(rss_microbenchmarks)

if(RSS_BUILD_TESTS)
    add_test(NAME MessageRouterBenchmark.ChatFanoutSmoke
        COMMAND rss_microbenchmarks --benchmark_filter=MessageRouterFixture/ChatFanout --benchmark_dry_run)
    add_executable(rss_benchmark_error_test BenchmarkMain.cpp tests/ErrorBenchmark.cpp)
    target_link_libraries(rss_benchmark_error_test PRIVATE benchmark::benchmark)
    rss_enable_project_warnings(rss_benchmark_error_test)
    add_test(NAME BenchmarkReporter.RejectsFailedRun
        COMMAND rss_benchmark_error_test --benchmark_filter=BM_IntentionalFailure --benchmark_dry_run)
    set_tests_properties(BenchmarkReporter.RejectsFailedRun PROPERTIES WILL_FAIL TRUE)
    add_test(NAME BenchmarkReporter.PreservesMixedRepetitionFailure
        COMMAND rss_benchmark_error_test --benchmark_filter=BM_MixedRepetitions
            --benchmark_report_aggregates_only=false --benchmark_display_aggregates_only=false)
    set_tests_properties(BenchmarkReporter.PreservesMixedRepetitionFailure PROPERTIES WILL_FAIL TRUE)
    foreach(mode IN ITEMS report display)
        add_test(NAME BenchmarkReporter.Rejects${mode}AggregatesOnly
            COMMAND rss_benchmark_error_test --benchmark_filter=BM_MixedRepetitions --benchmark_${mode}_aggregates_only=true)
        set_tests_properties(BenchmarkReporter.Rejects${mode}AggregatesOnly PROPERTIES WILL_FAIL TRUE)
        string(TOUPPER "${mode}" mode_upper)
        add_test(NAME BenchmarkReporter.Rejects${mode}AggregatesOnlyEnvironment
            COMMAND rss_benchmark_error_test --benchmark_filter=BM_MixedRepetitions)
        set_tests_properties(BenchmarkReporter.Rejects${mode}AggregatesOnlyEnvironment PROPERTIES
            WILL_FAIL TRUE ENVIRONMENT "BENCHMARK_${mode_upper}_AGGREGATES_ONLY=true")
    endforeach()
endif()
