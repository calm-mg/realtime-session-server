add_library(rss_qt_client_network STATIC
    src/QtSessionClient.cpp
    include/rss/qt_client/network/QtSessionClient.h
)

target_include_directories(rss_qt_client_network
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rss_qt_client_network
    PUBLIC
        rss_qt_client_application
        Qt6::Network
        rss_protocol
)

target_compile_features(rss_qt_client_network PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_qt_client_network)
