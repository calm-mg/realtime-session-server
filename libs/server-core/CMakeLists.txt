add_library(rss_server_core
    src/domain/Lobby.cpp
    src/domain/Room.cpp
    src/domain/User.cpp
    src/domain/UserId.cpp
    src/net/Session.cpp
    src/net/WorkerPool.cpp
    src/persistence/InMemoryUserRepository.cpp
    src/service/MessageRouter.cpp
    src/service/RoomService.cpp
)

target_include_directories(rss_server_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rss_server_core
    PUBLIC
        rss_protocol
        Threads::Threads
)

target_compile_features(rss_server_core PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_server_core)
