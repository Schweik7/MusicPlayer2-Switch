#pragma once
#include "Screen.h"
#include "../core/Lang.h"

// 当前播放列表。对应桌面版的播放列表面板。
class CPlaylistScreen : public CScreen
{
public:
    void OnEnter(ScreenContext& ctx) override;
    void Update(ScreenContext& ctx, double delta_seconds) override;
    void Draw(ScreenContext& ctx) override;

    const char* GetTitle() const override { return T("播放列表"); }
    const char* GetButtonHints() const override;

private:
    void EnsureSelectionVisible(int visible_count);
    // 处理手指拖动滚动；返回 true 表示本帧滚动位置由触摸决定，
    // 此时不能再让"光标可见"逻辑把列表拽回去
    bool UpdateTouchScroll(ScreenContext& ctx, int count, int visible, double delta_seconds);

    int m_selected{};
    int m_scroll{};                 // 顶部第一个可见项的下标
    double m_scroll_smooth{};       // 平滑滚动用的浮点位置

    bool m_dragging{};              // 手指正按在列表上拖动
    double m_fling{};               // 松手后的惯性速度（行/秒）
};
