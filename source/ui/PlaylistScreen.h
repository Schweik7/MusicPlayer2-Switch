#pragma once
#include "Screen.h"

// 当前播放列表。对应桌面版的播放列表面板。
class CPlaylistScreen : public CScreen
{
public:
    void OnEnter(ScreenContext& ctx) override;
    void Update(ScreenContext& ctx, double delta_seconds) override;
    void Draw(ScreenContext& ctx) override;

    const char* GetTitle() const override { return "播放列表"; }
    const char* GetButtonHints() const override;

private:
    void EnsureSelectionVisible(int visible_count);

    int m_selected{};
    int m_scroll{};                 // 顶部第一个可见项的下标
    double m_scroll_smooth{};       // 平滑滚动用的浮点位置
};
