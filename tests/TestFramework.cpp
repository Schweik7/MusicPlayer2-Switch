#include "TestFramework.h"

#include <cmath>

namespace TestFramework
{

int g_failed = 0;
int g_total = 0;

void Check(bool condition, const char* expr, const char* file, int line)
{
    ++g_total;
    if (!condition)
    {
        ++g_failed;
        std::printf("  [FAIL] %s  (%s:%d)\n", expr, file, line);
    }
}

void CheckEq(const std::string& actual, const std::string& expected, const char* expr,
             const char* file, int line)
{
    ++g_total;
    if (actual != expected)
    {
        ++g_failed;
        std::printf("  [FAIL] %s\n         expected: \"%s\"\n         actual:   \"%s\"  (%s:%d)\n",
                    expr, expected.c_str(), actual.c_str(), file, line);
    }
}

void CheckEqInt(long long actual, long long expected, const char* expr, const char* file, int line)
{
    ++g_total;
    if (actual != expected)
    {
        ++g_failed;
        std::printf("  [FAIL] %s\n         expected: %lld\n         actual:   %lld  (%s:%d)\n",
                    expr, expected, actual, file, line);
    }
}

void CheckNear(double actual, double expected, double tolerance, const char* expr,
               const char* file, int line)
{
    ++g_total;
    if (std::fabs(actual - expected) > tolerance)
    {
        ++g_failed;
        std::printf("  [FAIL] %s\n         expected: %.4f (±%.4f)\n         actual:   %.4f  (%s:%d)\n",
                    expr, expected, tolerance, actual, file, line);
    }
}

}   // namespace TestFramework
