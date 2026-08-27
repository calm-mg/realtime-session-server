add_library(rss_server_persistence_postgres
    src/PostgresExecutor.cpp
    src/PostgresUserRepository.cpp
)

target_include_directories(rss_server_persistence_postgres
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rss_server_persistence_postgres
    PUBLIC
        rss_server_core
    PRIVATE
        PostgreSQL::PostgreSQL
        Threads::Threads
)

target_compile_features(rss_server_persistence_postgres PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_server_persistence_postgres)
