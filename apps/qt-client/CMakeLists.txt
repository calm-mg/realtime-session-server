qt_add_executable(rss_qt_client
    src/main.cpp
)

target_link_libraries(rss_qt_client
    PRIVATE
        Qt6::Network
        Qt6::Widgets
        rss_protocol
)

rss_enable_project_warnings(rss_qt_client)

set_target_properties(rss_qt_client PROPERTIES
    MACOSX_BUNDLE ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE ON
)
