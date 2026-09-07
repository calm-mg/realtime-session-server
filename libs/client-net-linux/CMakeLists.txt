add_library(rss_client_net_linux
    src/ClientVersionNegotiation.cpp
)

target_include_directories(rss_client_net_linux
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(rss_client_net_linux PUBLIC rss_protocol)
target_compile_features(rss_client_net_linux PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_client_net_linux)
