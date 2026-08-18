add_library(rss_load_scenario_runtime
    src/EmbeddedServer.cpp
    src/EnvironmentInfo.cpp
    src/Program.cpp
    src/ScenarioClient.cpp
    src/ScenarioRunner.cpp
)
target_include_directories(rss_load_scenario_runtime
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(rss_load_scenario_runtime
    PUBLIC
        rss_load_test_support
    PRIVATE
        rss_server_net
)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE RSS_GIT_COMMIT_RESULT
    OUTPUT_VARIABLE RSS_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT RSS_GIT_COMMIT_RESULT EQUAL 0 OR RSS_GIT_COMMIT STREQUAL "")
    set(RSS_GIT_COMMIT "unknown")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "")
    set(RSS_BUILD_TYPE "unknown")
else()
    set(RSS_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()
target_compile_definitions(rss_load_scenario_runtime
    PRIVATE
        "RSS_GIT_COMMIT=\"${RSS_GIT_COMMIT}\""
        "RSS_COMPILER=\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\""
        "RSS_BUILD_TYPE=\"${RSS_BUILD_TYPE}\""
)
rss_enable_project_warnings(rss_load_scenario_runtime)

add_executable(rss_load_scenario_runner
    src/main.cpp
)
set_target_properties(rss_load_scenario_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(rss_load_scenario_runner
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(rss_load_scenario_runner
    PRIVATE
        rss_load_scenario_runtime
)
rss_enable_project_warnings(rss_load_scenario_runner)
