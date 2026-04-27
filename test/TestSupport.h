#pragma once

#include <cstdlib>
#include <iostream>
#include <string_view>

#define RSS_EXPECT(expr)                                                                 \
    do {                                                                                 \
        if (!(expr)) {                                                                   \
            std::cerr << "expectation failed: " #expr << " at " << __FILE__ << ':'      \
                      << __LINE__ << '\n';                                               \
            std::exit(EXIT_FAILURE);                                                     \
        }                                                                                \
    } while (false)

inline void testPassed(std::string_view name)
{
    std::cout << name << " passed\n";
}
