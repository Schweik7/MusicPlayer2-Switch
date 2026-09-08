#pragma once
#include <string>

// 系统日期时间。
//
// 不直接用 time()+localtime()：Switch 上 newlib 拿到的是 UTC，而时区信息要另外从
// 系统设置里取。libnx 的 TimeType_LocalSystemClock 返回的已经是本地时间了，
// 所以取到之后要用 gmtime 而不是 localtime 格式化，否则会再偏移一次时区。
namespace SystemClock
{
    struct DateTime
    {
        int year{ 1970 };
        int month{ 1 };         // 1-12
        int day{ 1 };           // 1-31
        int hour{};             // 0-23
        int minute{};
        int second{};
        int weekday{};          // 0=周日
        bool valid{};
    };

    void Init();
    void Uninit();

    DateTime Now();

    // "09/08 周一"
    std::string FormatDate(const DateTime& dt);
    // "21:34"
    std::string FormatTime(const DateTime& dt);
    // "2030-02-14 09:12"，用于日志和错误提示
    std::string FormatFull(const DateTime& dt);

    // 由 __DATE__ 推出的构建年份。
    int GetBuildYear();

    // 系统时钟是否明显不对。
    //
    // 判据是拿系统年份和构建年份比：程序不可能跑在比自己构建更早的年份，
    // 也很少会在构建两年之后还没更新过。时钟离谱会直接导致 TLS 校验失败
    // （证书被判成尚未生效或已过期），而 curl 只会报一句"证书验证失败"，
    // 用户根本想不到是时间的问题，所以值得专门认出来。
    bool IsClockImplausible();
}
