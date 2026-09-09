#pragma once
#include <string>

// 系统日期时间。
//
// 不直接用 time()+localtime()：Switch 上 newlib 拿到的是 UTC，时区要另外取。
//
// 也**不能**假设 TimeType_LocalSystemClock 已经是本地时间——实机上它返回的
// 就是 UTC，照着当本地时间用会整整差一个时区（UTC+8 下慢 8 小时）。
// 正确做法是拿 UTC 时间戳交给 timeToCalendarTimeWithMyRule()，
// 由系统按主机设置的时区规则换算，夏令时也一并处理掉。
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

        // 相对 UTC 的偏移（秒）和系统给出的时区名。
        // 留着是为了下次再出时间问题时能一眼看出时区到底套上没有——
        // 这次的 bug 表现成"慢 8 小时"，光看年月日时分是看不出病因的。
        int utc_offset_seconds{};
        std::string timezone;
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
