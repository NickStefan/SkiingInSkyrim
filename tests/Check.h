#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace Check
{
    struct Case
    {
        const char*           name;
        std::function<void()> body;
    };

    inline std::vector<Case>& Cases()
    {
        static std::vector<Case> cases;
        return cases;
    }

    inline int& Failures()
    {
        static int failures = 0;
        return failures;
    }

    struct Register
    {
        Register(const char* name, std::function<void()> body) { Cases().push_back({ name, std::move(body) }); }
    };

    inline void Fail(const char* file, int line, const char* expression)
    {
        std::printf("  FAILED %s:%d: %s\n", file, line, expression);
        ++Failures();
    }
}

#define CHECK_CONCAT_(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT_(a, b)
#define TEST_CASE(name)                                                             \
    static void CHECK_CONCAT(test_, __LINE__)();                                    \
    static Check::Register CHECK_CONCAT(register_, __LINE__){ name, CHECK_CONCAT(test_, __LINE__) }; \
    static void CHECK_CONCAT(test_, __LINE__)()
#define CHECK(condition) \
    ((condition) ? void() : Check::Fail(__FILE__, __LINE__, #condition))
#define CHECK_NEAR(actual, expected, tolerance) \
    CHECK(std::fabs((actual) - (expected)) <= (tolerance))
