add_library(rss_qt_client_lib STATIC
    src/application/ChatLogEntry.h
    src/application/ClientController.cpp
    src/application/ClientController.h
    src/application/ClientState.h
    src/network/QtSessionClient.cpp
    src/network/QtSessionClient.h
    src/network/SessionTransport.h
    src/ui/ChatBubbleDelegate.cpp
    src/ui/ChatBubbleDelegate.h
    src/ui/ChatLogModel.cpp
    src/ui/ChatLogModel.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.ui
    resources/client.qrc
)

set_target_properties(rss_qt_client_lib PROPERTIES AUTORCC ON)

target_include_directories(rss_qt_client_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rss_qt_client_lib
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Svg
        Qt6::Widgets
        rss_protocol
)

target_compile_features(rss_qt_client_lib PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_qt_client_lib)
if(MSVC)
    target_compile_options(rss_qt_client_lib PRIVATE /utf-8)
endif()

qt_add_executable(rss_qt_client
    src/main.cpp
)

target_link_libraries(rss_qt_client
    PRIVATE
        rss_qt_client_lib
        Qt6::Network
        Qt6::Widgets
)

rss_enable_project_warnings(rss_qt_client)

set_target_properties(rss_qt_client PROPERTIES
    MACOSX_BUNDLE ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE ON
)
