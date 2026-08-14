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
