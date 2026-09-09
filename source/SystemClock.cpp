#include "SystemClock.h"

#include <switch.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include "core/Lang.h"

namespace
{
    bool g_inited = false;

    // 这里存中文原文，翻译放到取值处。
    // 这个数组是命名空间作用域的，初始化发生在 main 之前——那时
    // Lang::SetLanguage 还没跑，在定义处调 T() 会把语言永久冻结在中文。
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
    if (g_inited && R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &timestamp)))
    {
        // 取 UTC 时间戳，换算交给系统。
        //
        // 原来读的是 TimeType_LocalSystemClock 再用 gmtime 格式化，前提是
        // "那个时钟已经是本地时间"——实机上不成立，它返回的是 UTC，
        // 于是显示的时间整整差一个时区。timeToCalendarTimeWithMyRule
        // 按主机设置的时区规则换算，夏令时也一并算好。
        TimeCalendarTime cal{};
        TimeCalendarAdditionalInfo info{};
        if (R_SUCCEEDED(timeToCalendarTimeWithMyRule(timestamp, &cal, &info)))
        {
            result.year = cal.year;
            result.month = cal.month;
            result.day = cal.day;
            result.hour = cal.hour;
            result.minute = cal.minute;
            result.second = cal.second;
            result.weekday = static_cast<int>(info.wday);
            result.utc_offset_seconds = info.offset;
            // timezoneName 不保证有结尾的 NUL，按定长截断
            result.timezone.assign(info.timezoneName,
                                   strnlen(info.timezoneName, sizeof(info.timezoneName)));
            result.valid = true;
            return result;
        }
    }

    // 退路：时区服务不可用时至少给出 UTC，总比不显示强。
    // valid 留 false，顶栏据此把时间画成灰的——不准要让人看得出来。
    if (timestamp == 0)
        timestamp = static_cast<u64>(std::time(nullptr));
    if (timestamp == 0)
        return result;

    std::time_t raw = static_cast<std::time_t>(timestamp);
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
    return result;
}

std::string FormatDate(const DateTime& dt)
{
    char buff[64];
    const char* weekday = (dt.weekday >= 0 && dt.weekday < 7)
                          ? T(kWeekdayNames[dt.weekday]) : "";
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
