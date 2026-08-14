qt_add_executable(rss_qt_client_controller_tests
    ClientControllerTest.cpp
)

target_link_libraries(rss_qt_client_controller_tests
    PRIVATE
        rss_qt_client_lib
        Qt6::Test
)

add_test(
    NAME rss_qt_client_controller_tests
    COMMAND rss_qt_client_controller_tests
)

qt_add_executable(rss_qt_session_client_tests
    QtSessionClientTest.cpp
)

target_link_libraries(rss_qt_session_client_tests
    PRIVATE
        rss_qt_client_lib
        Qt6::Network
        Qt6::Test
)

add_test(
    NAME rss_qt_session_client_tests
    COMMAND rss_qt_session_client_tests
)

qt_add_executable(rss_qt_main_window_tests
    MainWindowTest.cpp
)

target_link_libraries(rss_qt_main_window_tests
    PRIVATE
        rss_qt_client_lib
        Qt6::Test
        Qt6::Widgets
)

if(MSVC)
    target_compile_options(rss_qt_main_window_tests PRIVATE /utf-8)
endif()

add_test(
    NAME rss_qt_main_window_tests
    COMMAND rss_qt_main_window_tests
)

set_tests_properties(rss_qt_main_window_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
