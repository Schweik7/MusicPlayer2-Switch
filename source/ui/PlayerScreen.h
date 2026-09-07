#pragma once
#include "Screen.h"
#include <string>

struct SDL_Texture;

// 播放界面：封面 + 曲目信息 + 进度条 + 歌词/频谱。
// 对应桌面版的主界面，但按手柄操作和 720p 屏幕重新排布。
class CPlayerScreen : public CScreen
{
public:
    enum ViewMode
    {
        VIEW_LYRIC = 0,     // 歌词滚动（有歌词时的默认视图）
        VIEW_SPECTRUM,      // 频谱
        VIEW_COUNT
    };

    void OnEnter(ScreenContext& ctx) override;
    void ReleaseResources(ScreenContext& ctx) override;
    void Update(ScreenContext& ctx, double delta_seconds) override;
    void Draw(ScreenContext& ctx) override;

    const char* GetTitle() const override { return "正在播放"; }
    const char* GetButtonHints() const override;

private:
    void DrawCover(ScreenContext& ctx, int x, int y, int size);
    void DrawSongInfo(ScreenContext& ctx, int x, int y, int width);
    void DrawProgressBar(ScreenContext& ctx, int x, int y, int width);
    void DrawLyricView(ScreenContext& ctx, int x, int y, int width, int height);
    void DrawSpectrumView(ScreenContext& ctx, int x, int y, int width, int height);
    void DrawVolumeOverlay(ScreenContext& ctx);

    // 换歌时重新加载封面
    void RefreshCover(ScreenContext& ctx);

    ViewMode m_view{ VIEW_LYRIC };
    SDL_Texture* m_cover{};
    std::string m_cover_source;             // 当前封面对应的音频文件，用来判断是否需要重载

    double m_lyric_scroll{};                // 歌词滚动的当前偏移（像素），用于平滑过渡
    int m_last_lyric_index{ -1 };

    double m_volume_overlay_timer{};        // 调节音量后短暂显示的浮层
    bool m_seeking{};                       // 右摇杆拖动进度中
    double m_seek_accumulator{};
};
