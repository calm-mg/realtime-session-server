add_library(rss_load_test_support
    src/ScenarioOptions.cpp
    src/ScenarioReport.cpp
)

target_include_directories(rss_load_test_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(rss_load_test_support PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_load_test_support)
