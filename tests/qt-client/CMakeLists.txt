qt_add_executable(rss_qt_client_controller_tests
    ClientControllerTest.cpp
)

target_link_libraries(rss_qt_client_controller_tests
    PRIVATE rss_qt_client_application Qt6::Test
)

add_test(
    NAME rss_qt_client_controller_tests
    COMMAND rss_qt_client_controller_tests
)

qt_add_executable(rss_qt_chat_log_model_tests
    ChatLogModelTest.cpp
)

target_link_libraries(rss_qt_chat_log_model_tests
    PRIVATE
        rss_qt_client_ui
        Qt6::Test
)

target_include_directories(rss_qt_chat_log_model_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/apps/qt-client/ui/src
)

add_test(
    NAME rss_qt_chat_log_model_tests
    COMMAND rss_qt_chat_log_model_tests
)

qt_add_executable(rss_qt_session_client_tests
    QtSessionClientTest.cpp
)

target_link_libraries(rss_qt_session_client_tests
    PRIVATE rss_qt_client_network Qt6::Test
)

foreach(test_case
        emitsConnectedAndDisconnected
        receivesPacketSplitAcrossWrites
        receivesMultiplePacketsFromOneWrite
        reconnectsOnceAfterFatalProtocolError
        reconnectsAfterFatalSocketError
        sendsEncodedPacketsInOrder
        keepsConnectionUsableAfterOversizedOutgoingPayload
        rejectsSendWhileDisconnected)
    add_test(
        NAME rss_qt_session_client_tests.${test_case}
        COMMAND rss_qt_session_client_tests ${test_case}
    )
endforeach()

qt_add_executable(rss_qt_main_window_tests
    MainWindowTest.cpp
)

target_link_libraries(rss_qt_main_window_tests
    PRIVATE
        rss_qt_client_application
        rss_qt_client_ui
        Qt6::Test
)

target_include_directories(rss_qt_main_window_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/apps/qt-client/ui/src
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
