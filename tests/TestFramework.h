#pragma once
// 主机端测试用的极简断言框架，被 host_test.cpp 和 net_test.cpp 共用。
#include <cstdio>
#include <string>

namespace TestFramework
{
    extern int g_failed;
    extern int g_total;

    void Check(bool condition, const char* expr, const char* file, int line);
    void CheckEq(const std::string& actual, const std::string& expected, const char* expr,
                 const char* file, int line);
    void CheckEqInt(long long actual, long long expected, const char* expr,
                    const char* file, int line);
    void CheckNear(double actual, double expected, double tolerance, const char* expr,
                   const char* file, int line);
}

#define CHECK(expr)              TestFramework::Check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b)           TestFramework::CheckEq((a), (b), #a, __FILE__, __LINE__)
#define CHECK_EQ_INT(a, b)       TestFramework::CheckEqInt((a), (b), #a, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol)    TestFramework::CheckNear((a), (b), (tol), #a, __FILE__, __LINE__)
