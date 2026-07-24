include(FetchContent)

function(rss_enable_googletest)
    if(TARGET GTest::gtest_main)
        return()
    endif()

    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        googletest
        URL
            "https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
endfunction()
