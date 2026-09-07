#pragma once
#include <cstdint>

// 配色。基调参考 Switch 系统 UI 的深色主题，强调色沿用 MusicPlayer2 的蓝色。
struct Color
{
    uint8_t r{}, g{}, b{}, a{ 255 };

    constexpr Color() = default;
    constexpr Color(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a = 255)
        : r{ _r }, g{ _g }, b{ _b }, a{ _a } {}

    constexpr Color WithAlpha(uint8_t alpha) const { return Color{ r, g, b, alpha }; }
};

namespace Theme
{
    // 背景
    constexpr Color kBackground{ 0x1F, 0x1F, 0x27 };
    constexpr Color kPanel{ 0x2A, 0x2A, 0x35 };
    constexpr Color kPanelAlt{ 0x33, 0x33, 0x40 };
    constexpr Color kSeparator{ 0x44, 0x44, 0x52 };

    // 前景
    constexpr Color kText{ 0xF0, 0xF0, 0xF5 };
    constexpr Color kTextDim{ 0x9A, 0x9A, 0xAA };
    constexpr Color kTextDisabled{ 0x66, 0x66, 0x75 };

    // 强调色
    constexpr Color kAccent{ 0x3D, 0x9B, 0xE9 };
    constexpr Color kAccentDim{ 0x2A, 0x6C, 0xA3 };
    constexpr Color kSelection{ 0x3D, 0x9B, 0xE9, 0x55 };
    constexpr Color kHighlight{ 0xFF, 0xC1, 0x07 };

    // 频谱渐变的两端
    constexpr Color kSpectrumLow{ 0x3D, 0x9B, 0xE9 };
    constexpr Color kSpectrumHigh{ 0xE9, 0x3D, 0x9B };

    // 歌词
    constexpr Color kLyricCurrent{ 0xFF, 0xFF, 0xFF };
    constexpr Color kLyricOther{ 0x88, 0x88, 0x99 };
    constexpr Color kLyricKaraoke{ 0x3D, 0x9B, 0xE9 };
    constexpr Color kLyricTranslate{ 0xB0, 0xB0, 0xC0 };

    // 布局：逻辑分辨率固定 1280x720，掌机与底座模式共用一套坐标
    constexpr int kScreenWidth = 1280;
    constexpr int kScreenHeight = 720;
    constexpr int kHeaderHeight = 72;
    constexpr int kFooterHeight = 64;
    constexpr int kPadding = 24;
    constexpr int kListItemHeight = 56;
}
