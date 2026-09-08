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

    // 封面文件在外部被改写（下载完成）后调用，强制下一帧重新加载
    void InvalidateCover(ScreenContext& ctx);
    void Update(ScreenContext& ctx, double delta_seconds) override;
    void Draw(ScreenContext& ctx) override;

    const char* GetTitle() const override { return "正在播放"; }
    const char* GetButtonHints() const override;

private:
    // 触摸能按到的按钮。按十字排布，位置与方向键一一对应：
    // 上=播放/暂停，左=上一曲，右=下一曲，下=停止。
    // 这样屏幕上的按钮本身就是键位说明，底栏不用再写方向键指引。
    enum HitButton
    {
        HIT_NONE = 0,
        HIT_PREV,
        HIT_PLAY,
        HIT_NEXT,
        HIT_STOP
    };

    void DrawCover(ScreenContext& ctx);
    void DrawSongInfo(ScreenContext& ctx);
    void DrawProgressBar(ScreenContext& ctx);
    void DrawTransportButtons(ScreenContext& ctx);
    void DrawLyricView(ScreenContext& ctx, int x, int y, int width, int height);
    void DrawSpectrumView(ScreenContext& ctx, int x, int y, int width, int height);
    void DrawVolumeOverlay(ScreenContext& ctx);

    void HandleTouch(ScreenContext& ctx);
    // 进度条上要显示的位置：正常是播放位置，拖动时是拖到的位置
    int  GetDisplayPosition(ScreenContext& ctx) const;

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

    bool m_touch_seeking{};                 // 手指按在进度条上拖动中
    int  m_touch_seek_ms{};
    HitButton m_pressed_button{ HIT_NONE }; // 当前被手指按住的按钮，用于按下态高亮
};
