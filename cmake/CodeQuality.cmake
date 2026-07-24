file(
    GLOB_RECURSE RSS_CXX_FORMAT_FILES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/client/*.cpp"
    "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/test/*.cpp"
    "${PROJECT_SOURCE_DIR}/test/*.h"
    "${PROJECT_SOURCE_DIR}/test/*.hpp"
    "${PROJECT_SOURCE_DIR}/tools/*.cpp"
)

find_program(
    RSS_CLANG_FORMAT
    NAMES clang-format-18 clang-format
)

if(RSS_CLANG_FORMAT)
    add_custom_target(
        format
        COMMAND "${RSS_CLANG_FORMAT}" -i ${RSS_CXX_FORMAT_FILES}
        COMMENT "Formatting C++ sources with ${RSS_CLANG_FORMAT}"
        VERBATIM
    )

    add_custom_target(
        format-check
        COMMAND
            "${RSS_CLANG_FORMAT}"
            --dry-run
            --Werror
            ${RSS_CXX_FORMAT_FILES}
        COMMENT "Checking C++ formatting with ${RSS_CLANG_FORMAT}"
        VERBATIM
    )
else()
    message(STATUS "clang-format was not found; format targets are unavailable.")
endif()

find_program(
    RSS_RUN_CLANG_TIDY
    NAMES run-clang-tidy-18 run-clang-tidy
)

if(RSS_RUN_CLANG_TIDY)
    add_custom_target(
        tidy-check
        COMMAND
            "${RSS_RUN_CLANG_TIDY}"
            -p "${CMAKE_BINARY_DIR}"
            -config-file "${PROJECT_SOURCE_DIR}/.clang-tidy"
            -header-filter ".*"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Running Google clang-tidy checks"
        VERBATIM
    )
else()
    message(STATUS "run-clang-tidy was not found; tidy-check is unavailable.")
endif()
