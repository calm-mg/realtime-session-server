add_library(rss_load_test_support INTERFACE)

target_include_directories(rss_load_test_support
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(rss_load_test_support INTERFACE cxx_std_20)
