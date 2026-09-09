#pragma once
#include "Screen.h"
#include "Theme.h"
#include "../core/FileUtil.h"
#include "../core/Lang.h"

#include <string>
#include <vector>

// SD 卡文件浏览：进目录、播放单曲、把整个目录加入播放列表、打开播放列表文件。
class CBrowserScreen : public CScreen
{
public:
    void OnEnter(ScreenContext& ctx) override;
    void Update(ScreenContext& ctx, double delta_seconds) override;
    void Draw(ScreenContext& ctx) override;

    const char* GetTitle() const override { return T("浏览 SD 卡"); }
    const char* GetButtonHints() const override;
    void GoBack(ScreenContext& ctx) override;

private:
    struct Item
    {
        std::string name;
        bool is_dir{};
        bool is_audio{};
        bool is_playlist{};
    };

    void Navigate(ScreenContext& ctx, const std::string& dir);
    void GoUp(ScreenContext& ctx);
    void Activate(ScreenContext& ctx);
    // 把当前目录里的音频文件全部加入播放列表并从选中项开始播放
    void PlayCurrentDirectory(ScreenContext& ctx, bool from_selection);
    // 设为默认音乐目录：写配置、立刻落盘，并把目录扫进播放列表
    void SetAsMusicDir(ScreenContext& ctx);
    void EnsureSelectionVisible(int visible_count);

    std::string m_dir;
    std::vector<Item> m_items;
    int m_selected{};
    int m_scroll{};
    double m_scroll_smooth{};
    bool m_dragging{};              // 手指正按在列表上拖动
    double m_fling{};               // 松手后的惯性速度（行/秒）
    std::string m_error;

    // 路径行右侧的两个动作按钮。这两个功能原本只有 X / Y 键能触发，
    // 纯触摸操作就用不了。矩形在 Draw 里算出，供下一帧命中判定。
    Rect m_play_dir_button;
    Rect m_set_dir_button;
};
