#pragma once
#include <cstdint>

// 配色。深色基调参考 Switch 系统 UI，强调色沿用 MusicPlayer2 的蓝色；
// 另有一套浅色配色，可在设置里切换。
struct Color
{
    uint8_t r{}, g{}, b{}, a{ 255 };

    constexpr Color() = default;
    constexpr Color(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a = 255)
        : r{ _r }, g{ _g }, b{ _b }, a{ _a } {}

    constexpr Color WithAlpha(uint8_t alpha) const { return Color{ r, g, b, alpha }; }
};

// 触摸命中判定用的矩形。绘制和命中要共用同一份坐标，
// 否则很容易画一套、点另一套。
struct Rect
{
    int x{}, y{}, w{}, h{};

    bool Contains(int px, int py) const
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

namespace Theme
{
    enum Mode
    {
        MODE_DARK = 0,
        MODE_LIGHT = 1
    };

    // 切换配色。全局改一次，所有界面下一帧就跟着变——
    // 各处引用的是下面这些变量本身，不需要逐个界面通知。
    void SetMode(Mode mode);
    Mode GetMode();

    // 这些颜色刻意不是 constexpr：要能在运行时整体换一套。
    // 名字和取值方式保持不变，所以 300 多处引用一处都不用改。
    //
    // 代价是它们不能再用在常量表达式里。目前没有这样的用法，
    // 真要加的时候编译期就会报出来，不会悄悄出错。

    // 背景
    extern Color kBackground;
    extern Color kPanel;
    extern Color kPanelAlt;
    extern Color kSeparator;

    // 前景
    extern Color kText;
    extern Color kTextDim;
    extern Color kTextDisabled;

    // 强调色
    extern Color kAccent;
    extern Color kAccentDim;
    extern Color kSelection;
    extern Color kHighlight;

    // 频谱渐变的两端
    extern Color kSpectrumLow;
    extern Color kSpectrumHigh;

    // 歌词
    extern Color kLyricCurrent;
    extern Color kLyricOther;
    extern Color kLyricKaraoke;
    extern Color kLyricTranslate;

    // 电量图标
    extern Color kBatteryLow;
    extern Color kBatteryCharging;

    // 布局：逻辑分辨率固定 1280x720，掌机与底座模式共用一套坐标。
    // 这些仍然是编译期常量，换配色不影响排版。
    constexpr int kScreenWidth = 1280;
    constexpr int kScreenHeight = 720;
    constexpr int kHeaderHeight = 72;
    constexpr int kFooterHeight = 64;
    constexpr int kPadding = 24;
    constexpr int kListItemHeight = 56;
}
