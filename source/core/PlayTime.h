#pragma once
#include <string>
#include <cstdio>

// 与桌面版 CPlayTime 保持等价语义的可移植实现（去掉了 wchar_t / MFC 依赖）
class CPlayTime
{
public:
    unsigned int negative : 1;
    unsigned int min : 15;
    unsigned int sec : 6;
    unsigned int msec : 10;

    CPlayTime() : negative{}, min{}, sec{}, msec{} {}

    CPlayTime(int _min, int _sec, int _msec)
        : negative{}, min{ static_cast<unsigned int>(_min) },
          sec{ static_cast<unsigned int>(_sec) }, msec{ static_cast<unsigned int>(_msec) } {}

    explicit CPlayTime(int time) : negative{}, min{}, sec{}, msec{} { fromInt(time); }

    // 将int类型的时间（毫秒数）转换成Time结构
    void fromInt(int time)
    {
        if (time < 0)
        {
            negative = 1;
            time = -time;
        }
        else
        {
            negative = 0;
        }
        msec = time % 1000;
        sec = time / 1000 % 60;
        min = time / 1000 / 60;
    }

    // 将Time结构转换成int类型（毫秒数）
    int toInt() const
    {
        int t = static_cast<int>(msec) + static_cast<int>(sec) * 1000 + static_cast<int>(min) * 60000;
        return negative ? -t : t;
    }

    bool operator>(const CPlayTime& t) const { return toInt() > t.toInt(); }
    bool operator<(const CPlayTime& t) const { return toInt() < t.toInt(); }
    bool operator>=(const CPlayTime& t) const { return toInt() >= t.toInt(); }
    bool operator<=(const CPlayTime& t) const { return toInt() <= t.toInt(); }
    bool operator==(const CPlayTime& t) const { return toInt() == t.toInt(); }
    bool operator!=(const CPlayTime& t) const { return toInt() != t.toInt(); }

    // 减法运算符，用于计算两个Time对象的时间差，返回int类型，单位为毫秒
    int operator-(const CPlayTime& t) const { return toInt() - t.toInt(); }

    CPlayTime& operator+=(int time) { fromInt(toInt() + time); return *this; }
    CPlayTime& operator-=(int time) { return operator+=(-time); }
    CPlayTime operator+(int time) const { return CPlayTime{ toInt() + time }; }

    // 将时间转换成字符串（格式：分:秒）
    std::string toString(bool no_zero = true) const
    {
        char buff[24]{};
        if (no_zero && isZero())
            std::snprintf(buff, sizeof(buff), "-:--");
        else
            std::snprintf(buff, sizeof(buff), "%u:%.2u", min, sec);
        return buff;
    }

    // 将时间转换成字符串（格式：时:分:秒）
    std::string toString3(bool no_zero = true) const
    {
        char buff[24]{};
        if (no_zero && isZero())
            std::snprintf(buff, sizeof(buff), "-:--:--");
        else
            std::snprintf(buff, sizeof(buff), "%u:%.2u:%.2u", min / 60, min % 60, sec);
        return buff;
    }

    // 判断时间是否为0
    bool isZero() const { return min == 0 && sec == 0 && msec == 0; }
};
