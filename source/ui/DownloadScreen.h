#pragma once
#include "Screen.h"
#include "Theme.h"
#include "../net/DownloadManager.h"
#include "../core/Lang.h"

#include <string>

// 在线歌词/封面下载界面。
//
// 对应桌面版的"歌词下载"对话框：显示搜索关键词、结果列表，选中后下载歌词与封面。
// 网络请求都在 CDownloadManager 的后台线程里跑，这里只负责每帧读状态和画界面。
class CDownloadScreen : public CScreen
{
public:
    void OnEnter(ScreenContext& ctx) override;
    void OnLeave(ScreenContext& ctx) override;
    void Update(ScreenContext& ctx, double delta_seconds) override;
    void Draw(ScreenContext& ctx) override;

    const char* GetTitle() const override { return T("在线下载"); }
    const char* GetButtonHints() const override;

private:
    void StartSearch(ScreenContext& ctx);
    void StartDownload(ScreenContext& ctx, int index);
    void EditKeyword(ScreenContext& ctx);
    // 顶部那几个开关。抽成方法是为了让手柄键和触摸走同一段代码，
    // 不至于改了一边忘了另一边。
    void ToggleProvider(ScreenContext& ctx);
    void ToggleLyric(ScreenContext& ctx);
    void ToggleCover(ScreenContext& ctx);
    void ToggleEmbed(ScreenContext& ctx);
    // 处理顶部那一行的点击。命中了返回 true。
    bool HandleTopBarTouch(ScreenContext& ctx);
    // 用当前曲目的标签生成默认关键词
    void ResetKeywordFromCurrentSong(ScreenContext& ctx);
    CDownloadManager::AutoRequest MakeRequest(ScreenContext& ctx) const;
    void EnsureSelectionVisible(int visible_count);

    std::string m_keyword;
    std::string m_song_path;            // 进入界面时的曲目，用于判断是否需要重置关键词

    int m_selected{};
    int m_scroll{};
    double m_scroll_smooth{};

    bool m_download_lyric{ true };
    bool m_download_cover{ true };
    double m_spinner_phase{};           // 忙碌指示器的动画相位

    // 顶部那一行的可点区域。全都是原本只有手柄键能触发的功能，
    // 不给触摸入口的话纯触摸操作就搜不了、也换不了音乐源。
    // 矩形在 Draw 里按文字宽度算出，供下一帧的命中判定用。
    Rect m_keyword_rect;
    Rect m_provider_rect;
    Rect m_lyric_rect;
    Rect m_cover_rect;
    Rect m_embed_rect;
    // 忙碌时状态行右侧的取消按钮。取消原本只有 − 键能按。
    Rect m_cancel_rect;
};
