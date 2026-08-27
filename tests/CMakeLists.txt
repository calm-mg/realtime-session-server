add_subdirectory(protocol)
add_subdirectory(server-core)
add_subdirectory(load-test-support)

if(RSS_BUILD_POSTGRES)
    add_subdirectory(server-persistence-postgres)
endif()

if(RSS_BUILD_NETWORK_TARGETS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(server-net-linux)
endif()

if(RSS_BUILD_QT_CLIENT)
    add_subdirectory(qt-client)
endif()
