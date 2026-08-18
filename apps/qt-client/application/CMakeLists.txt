add_library(rss_qt_client_application STATIC
    src/ClientController.cpp
    include/rss/qt_client/application/ChatLogEntry.h
    include/rss/qt_client/application/ClientController.h
    include/rss/qt_client/application/ClientState.h
    include/rss/qt_client/application/SessionTransport.h
)

target_include_directories(rss_qt_client_application
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rss_qt_client_application
    PUBLIC Qt6::Core rss_protocol
)

target_compile_features(rss_qt_client_application PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_qt_client_application)
