add_library(rss_qt_client_ui STATIC
    src/ChatBubbleDelegate.cpp
    src/ChatBubbleDelegate.h
    src/ChatLogModel.cpp
    src/ChatLogModel.h
    src/MainWindow.cpp
    src/MainWindow.ui
    include/rss/qt_client/ui/MainWindow.h
    ../resources/client.qrc
)

set_target_properties(rss_qt_client_ui PROPERTIES AUTORCC ON)

target_include_directories(rss_qt_client_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rss_qt_client_ui
    PUBLIC rss_qt_client_application Qt6::Widgets
    PRIVATE Qt6::Svg
)

target_compile_features(rss_qt_client_ui PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_qt_client_ui)
if(MSVC)
    target_compile_options(rss_qt_client_ui PRIVATE /utf-8)
endif()
