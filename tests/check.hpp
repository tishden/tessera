// Copyright 2026 Denis Tishkov
// SPDX-License-Identifier: Apache-2.0

#ifndef TESSERA_TESTS_CHECK_HPP
#define TESSERA_TESTS_CHECK_HPP

#include <cstdio>
#include <string_view>
#include <vector>

/// @file check.hpp
/// @brief A ~60-line test harness. Tessera is header-only and dependency-free, and so is its
///        test suite: `cmake --build . && ctest` works on a machine with nothing installed.
///
/// Most of what there is to test about a compile-time library is tested by `static_assert` inside
/// the test files — if the suite compiles, those passed. The harness covers the rest: stored
/// values, iteration order, constructor semantics.

namespace tessera_test {

using test_function = void (*)();

struct test_case {
    std::string_view name;
    test_function run;
};

inline std::vector<test_case>& registry() {
    static std::vector<test_case> cases;
    return cases;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

struct registrar {
    registrar(std::string_view name, test_function run) {
        registry().push_back({name, run});
    }
};

inline void report_failure(std::string_view file, int line, std::string_view expression) {
    ++failure_count();
    std::printf("  FAILED  %.*s:%d: %.*s\n", static_cast<int>(file.size()), file.data(), line,
                static_cast<int>(expression.size()), expression.data());
}

inline int run_all() {
    int failed = 0;
    for (const test_case& test : registry()) {
        const int before = failure_count();
        test.run();
        const bool ok = failure_count() == before;
        failed += ok ? 0 : 1;
        std::printf("[%s] %.*s\n", ok ? "PASS" : "FAIL", static_cast<int>(test.name.size()), test.name.data());
    }
    std::printf("\n%zu tests, %d failed\n", registry().size(), failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace tessera_test

/// @brief Defines and registers a test case.
#define TESSERA_TEST(name)                                                 \
    static void name();                                                    \
    static const ::tessera_test::registrar registrar_##name{#name, &name}; \
    static void name()

/// @brief Runtime assertion; failures are reported and the suite keeps going.
/// Variadic so that expressions containing commas (`f<int, double>()`) need no extra parentheses.
#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            ::tessera_test::report_failure(__FILE__, __LINE__, #__VA_ARGS__); \
        }                                                                     \
    } while (false)

#define CHECK_EQ(lhs, rhs) CHECK((lhs) == (rhs))

#endif  // TESSERA_TESTS_CHECK_HPP
