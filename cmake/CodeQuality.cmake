file(
    GLOB_RECURSE RSS_CXX_FORMAT_FILES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/apps/*.cpp"
    "${PROJECT_SOURCE_DIR}/apps/*.h"
    "${PROJECT_SOURCE_DIR}/apps/*.hpp"
    "${PROJECT_SOURCE_DIR}/benchmarks/*.cpp"
    "${PROJECT_SOURCE_DIR}/libs/*.cpp"
    "${PROJECT_SOURCE_DIR}/libs/*.h"
    "${PROJECT_SOURCE_DIR}/libs/*.hpp"
    "${PROJECT_SOURCE_DIR}/tests/*.cpp"
    "${PROJECT_SOURCE_DIR}/tests/*.h"
    "${PROJECT_SOURCE_DIR}/tests/*.hpp"
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
    # 소스 경로의 정규식 메타 문자를 이스케이프해 검사 누락을 방지합니다.
    set(RSS_SOURCE_DIR_REGEX "${PROJECT_SOURCE_DIR}")
    foreach(RSS_REGEX_CHAR IN ITEMS "\\" "." "+" "*" "?" "^" "$" "(" ")" "[" "]" "|")
        string(REPLACE "${RSS_REGEX_CHAR}" "\\${RSS_REGEX_CHAR}"
            RSS_SOURCE_DIR_REGEX "${RSS_SOURCE_DIR_REGEX}")
    endforeach()
    add_custom_target(
        tidy-check
        COMMAND
            "${RSS_RUN_CLANG_TIDY}"
            -p "${CMAKE_BINARY_DIR}"
            -config-file "${PROJECT_SOURCE_DIR}/.clang-tidy"
            -header-filter
            "^${RSS_SOURCE_DIR_REGEX}/(apps|benchmarks|libs|tests)/.*"
            "^${RSS_SOURCE_DIR_REGEX}/(apps|benchmarks|libs|tests)/.*\\.cpp$"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Running Google clang-tidy checks"
        VERBATIM
    )
else()
    message(STATUS "run-clang-tidy was not found; tidy-check is unavailable.")
endif()
