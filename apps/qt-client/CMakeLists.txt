add_subdirectory(application)
add_subdirectory(network)
add_subdirectory(ui)

qt_add_executable(rss_qt_client
    src/main.cpp
)

target_link_libraries(rss_qt_client
    PRIVATE
        rss_qt_client_network
        rss_qt_client_ui
        Qt6::Widgets
)

rss_enable_project_warnings(rss_qt_client)

set_target_properties(rss_qt_client PROPERTIES
    MACOSX_BUNDLE ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE ON
)
