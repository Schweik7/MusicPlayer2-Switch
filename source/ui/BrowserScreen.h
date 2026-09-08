#pragma once
#include "Screen.h"
#include "../core/FileUtil.h"

#include <string>
#include <vector>

// SD 卡文件浏览：进目录、播放单曲、把整个目录加入播放列表、打开播放列表文件。
class CBrowserScreen : public CScreen
{
public:
    void OnEnter(ScreenContext& ctx) override;
    void Update(ScreenContext& ctx, double delta_seconds) override;
    void Draw(ScreenContext& ctx) override;

    const char* GetTitle() const override { return "浏览 SD 卡"; }
    const char* GetButtonHints() const override;

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
    void EnsureSelectionVisible(int visible_count);

    std::string m_dir;
    std::vector<Item> m_items;
    int m_selected{};
    int m_scroll{};
    double m_scroll_smooth{};
    bool m_dragging{};              // 手指正按在列表上拖动
    double m_fling{};               // 松手后的惯性速度（行/秒）
    std::string m_error;
};
