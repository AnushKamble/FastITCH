#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <exception>

namespace fastitch::test {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& get_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

inline bool register_test(const std::string& name, std::function<void()> func) {
    get_registry().push_back({name, std::move(func)});
    return true;
}

inline int run_all_tests(int /*argc*/, char** /*argv*/) {
    int passed = 0;
    int failed = 0;
    const auto& tests = get_registry();
    std::cout << "[==========] Running " << tests.size() << " tests.\n";

    for (const auto& test : tests) {
        std::cout << "[ RUN      ] " << test.name << "\n";
        try {
            test.func();
            std::cout << "[       OK ] " << test.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cerr << "[  FAILED  ] " << test.name << " with exception: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cerr << "[  FAILED  ] " << test.name << " with unknown exception\n";
            ++failed;
        }
    }

    std::cout << "[==========] " << tests.size() << " tests ran.\n";
    std::cout << "[  PASSED  ] " << passed << " tests.\n";
    if (failed > 0) {
        std::cerr << "[  FAILED  ] " << failed << " tests.\n";
        return 1;
    }
    return 0;
}

#define TEST_CASE(name) \
    static void test_func_##name(); \
    static const bool registered_##name = ::fastitch::test::register_test(#name, test_func_##name); \
    static void test_func_##name()

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #condition + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

#define REQUIRE_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

#define REQUIRE_NE(a, b) \
    do { \
        if (!((a) != (b))) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #a + " != " + #b + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

#define REQUIRE_TRUE(condition) REQUIRE(condition)
#define REQUIRE_FALSE(condition) REQUIRE(!(condition))

} // namespace fastitch::test
