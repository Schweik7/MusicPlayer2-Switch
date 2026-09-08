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
}
