#include "SystemClock.h"

#include <switch.h>

#include <cstdio>
#include <ctime>

namespace
{
    bool g_inited = false;

    const char* const kWeekdayNames[7] = {
        "周日", "周一", "周二", "周三", "周四", "周五", "周六"
    };
}

namespace SystemClock
{

void Init()
{
    if (g_inited)
        return;
    // libnx 的服务初始化是引用计数的，即使运行时已经初始化过也不会出问题
    g_inited = R_SUCCEEDED(timeInitialize());
}

void Uninit()
{
    if (!g_inited)
        return;
    timeExit();
    g_inited = false;
}

DateTime Now()
{
    DateTime result;

    u64 timestamp = 0;
    bool got = g_inited
               && R_SUCCEEDED(timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp));
    if (!got)
    {
        // 退路：系统时钟服务不可用时至少给出 UTC，总比不显示强
        timestamp = static_cast<u64>(std::time(nullptr));
        if (timestamp == 0)
            return result;
    }

    std::time_t raw = static_cast<std::time_t>(timestamp);
    // timestamp 已经是本地时间，这里必须用 gmtime，用 localtime 会再叠加一次时区
    std::tm* tm_value = std::gmtime(&raw);
    if (tm_value == nullptr)
        return result;

    result.year = tm_value->tm_year + 1900;
    result.month = tm_value->tm_mon + 1;
    result.day = tm_value->tm_mday;
    result.hour = tm_value->tm_hour;
    result.minute = tm_value->tm_min;
    result.second = tm_value->tm_sec;
    result.weekday = tm_value->tm_wday;
    result.valid = got;
    return result;
}

std::string FormatDate(const DateTime& dt)
{
    char buff[64];
    const char* weekday = (dt.weekday >= 0 && dt.weekday < 7) ? kWeekdayNames[dt.weekday] : "";
    std::snprintf(buff, sizeof(buff), "%02d/%02d %s", dt.month, dt.day, weekday);
    return buff;
}

std::string FormatTime(const DateTime& dt)
{
    char buff[16];
    std::snprintf(buff, sizeof(buff), "%02d:%02d", dt.hour, dt.minute);
    return buff;
}

std::string FormatFull(const DateTime& dt)
{
    char buff[32];
    std::snprintf(buff, sizeof(buff), "%04d-%02d-%02d %02d:%02d",
                  dt.year, dt.month, dt.day, dt.hour, dt.minute);
    return buff;
}

int GetBuildYear()
{
    // __DATE__ 形如 "Sep  8 2026"，年份固定在第 7..10 个字符
    const char* date = __DATE__;
    int year = 0;
    for (int i = 7; i < 11 && date[i] >= '0' && date[i] <= '9'; ++i)
        year = year * 10 + (date[i] - '0');
    return year > 0 ? year : 2026;
}

bool IsClockImplausible()
{
    DateTime now = Now();
    if (!now.valid)
        return false;                   // 读不到时间就别乱下结论
    int build_year = GetBuildYear();
    return now.year < build_year || now.year > build_year + 2;
}

}   // namespace SystemClock
