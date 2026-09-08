#pragma once
#include "Screen.h"
#include "Theme.h"
#include "../core/LrcParser.h"

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
        VIEW_COVER,         // 大封面
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

    // 播放界面是根，平时没有上一层；只有封面全屏时"返回"才有意义
    bool CanGoBack() const override { return m_cover_fullscreen; }
    bool WantsFullScreen() const override { return m_cover_fullscreen; }
    void GoBack(ScreenContext& ctx) override;

private:
    // 内容区的尺寸。按钮簇的坐标是编译期常量（绘制与命中共用同一份），
    // 但封面、曲目信息、进度条、歌词区要随模式变，所以每帧算一次。
    struct Layout
    {
        int left_width{};
        Rect cover{};
        int info_y{};                       // 标题基线
        int progress_y{};
        Rect progress_hit{};                // 比可视进度条大得多，手指点不了那么准
        Rect right{};                       // 歌词 / 频谱 / 大封面
    };
    // immersive 时没有触摸按钮，左栏可以让封面占得更大；
    // hide_hints 时底栏不画，内容区跟着往下长。
    static Layout ComputeLayout(bool immersive, bool hide_hints);

    // 触摸能按到的按钮。
    //
    // 左下角的四个按十字排布，位置与方向键一一对应（上=播放/暂停，左=上一曲，
    // 右=下一曲，下=停止）；右下角的四个按 ABXY 的实际排布摆成菱形。
    // 屏幕上的布局本身就是键位说明，用户不必先读底栏再去找键。
    enum HitButton
    {
        HIT_NONE = 0,
        HIT_PREV,
        HIT_PLAY,
        HIT_NEXT,
        HIT_STOP,
        HIT_FACE_A,
        HIT_FACE_B,
        HIT_FACE_X,
        HIT_FACE_Y,
        HIT_TOOL_DOWNLOAD,      // 歌词区右上角：下载歌词/封面
        HIT_TOOL_SYNC,          // 歌词区右上角：拖歌词是否带着进度走
        HIT_TOOL_LAYOUT,        // 歌词区右上角：单栏 / 双栏
        HIT_TOOL_REPEAT,        // 歌词区右上角：播放模式
        HIT_VOLUME_MINUS,       // 左摇杆十字：下 = 音量减
        HIT_VOLUME_PLUS,        // 左摇杆十字：上 = 音量加
        HIT_SEEK_BACK,          // 左摇杆十字：左 = 后退 1 秒
        HIT_SEEK_FORWARD        // 左摇杆十字：右 = 前进 1 秒
    };

    void DrawCover(ScreenContext& ctx);
    // 歌词区背景图。压得很暗，否则歌词读不清——它是背景不是主角。
    void DrawLyricBackground(ScreenContext& ctx, int x, int y, int width, int height);
    // 背景图用的纹理，按设置和当前曲目决定；换歌或改设置时重新加载
    void RefreshBackground(ScreenContext& ctx);
    void DrawSongInfo(ScreenContext& ctx);
    void DrawProgressBar(ScreenContext& ctx);
    void DrawTransportButtons(ScreenContext& ctx);
    // 右下角的 ABXY 触摸键。只在单栏歌词/频谱下画：双栏时右半边是译文，会挡住。
    void DrawFaceButtons(ScreenContext& ctx);
    // 歌词区右上角的一排小按钮
    void DrawLyricTools(ScreenContext& ctx);
    // 播放模式的图标。五种模式要在 40 像素里分得清，所以用图形拼——
    // 两个汉字在这个尺寸下挤成一团，一个汉字又分不出"单曲循环"和"单曲播放"。
    void DrawRepeatIcon(ScreenContext& ctx, int cx, int cy, Color color);
    // 歌词区左上角的左摇杆十字：上下音量、左右逐秒进退，中心显示音量。
    // 和左下角的方向键十字、右下角的 ABXY 菱形并列，是第三个自带说明的操作区。
    void DrawStickCross(ScreenContext& ctx);
    bool FaceButtonsVisible(ScreenContext& ctx) const;
    // 当前是否按双栏排版显示歌词（要同时满足：设置开了、显示译文、这首歌真的有译文）
    bool IsTwoColumnLyric(ScreenContext& ctx) const;
    void DrawLyricView(ScreenContext& ctx, int x, int y, int width, int height);
    // 画一行原文（当前行且有分词信息时带逐字高亮）。
    // 抽出来是因为单栏和双栏都要用，只是给的横向范围不同。
    void DrawLyricText(ScreenContext& ctx, const CLrcParser::Lyric& line, int center_x, int y,
                       int max_width, bool is_current, int position);
    void DrawSpectrumView(ScreenContext& ctx, int x, int y, int width, int height);
    // 右栏铺满的大封面。存在的意义是让"看大图"这件事不只有触摸能做到：
    // X 键循环视图就能切到它，再点一下（或按 A）进全屏。
    void DrawCoverView(ScreenContext& ctx, int x, int y, int width, int height);
    void DrawVolumeOverlay(ScreenContext& ctx);
    // 封面全屏查看：点封面进入，再点一下或按 B 退出
    void DrawCoverFullscreen(ScreenContext& ctx);

    void HandleTouch(ScreenContext& ctx);
    // 按住实体键时把对应的屏上按钮也点亮。
    // 屏上那三个操作区本来就是照着实体键排的，按下时同步高亮，
    // 用户第一次按就能把"我按的键"和"屏幕上哪个格子"对上。
    void HighlightHeldButton(ScreenContext& ctx);
    // 手指在歌词区上下拖动：翻看歌词。返回 true 表示这一帧的触摸已被歌词区吃掉。
    bool HandleLyricDrag(ScreenContext& ctx, double delta_seconds);
    // 翻看结束（松手，或右摇杆回中）。开了同步就跳到停留的那一句。
    void FinishLyricBrowse(ScreenContext& ctx, bool moved);
    // 按下某个 ABXY 触摸键等价于按下对应的手柄键
    void ActivateFaceButton(ScreenContext& ctx, HitButton button);
    // 进度条上要显示的位置：正常是播放位置，拖动时是拖到的位置
    int  GetDisplayPosition(ScreenContext& ctx) const;

    // 换歌时重新加载封面
    void RefreshCover(ScreenContext& ctx);

    ViewMode m_view{ VIEW_LYRIC };
    SDL_Texture* m_cover{};
    std::string m_cover_source;             // 当前封面对应的音频文件，用来判断是否需要重载

    // 歌词区背景。只有"自定义图片"这一档需要自己持有纹理；
    // 用封面当背景时直接复用 m_cover，不重复占显存。
    SDL_Texture* m_background{};
    int m_background_mode{ -1 };            // 上次加载时的设置值，变了就重新加载

    double m_lyric_scroll{};                // 歌词滚动的当前偏移（像素），用于平滑过渡
    int m_last_lyric_index{ -1 };

    double m_volume_overlay_timer{};        // 调节音量后短暂显示的浮层
    bool m_seeking{};                       // 右摇杆拖动进度中
    double m_seek_accumulator{};

    bool m_cover_fullscreen{};              // 封面全屏查看中
    bool m_touch_seeking{};                 // 手指按在进度条上拖动中
    int  m_touch_seek_ms{};
    HitButton m_pressed_button{ HIT_NONE }; // 当前被手指按住的按钮，用于按下态高亮

    // ---- 歌词浏览 ----
    // 默认只是翻看：松手几秒后自己滑回当前播放的那句。
    // 打开"同步"后改成松手即定位，把歌曲进度也带过去。
    bool   m_lyric_dragging{};
    bool   m_lyric_stick_browsing{};        // 正在用右摇杆翻看
    double m_lyric_browse_offset{};         // 相对跟随位置的像素偏移，向下拖为正
    double m_lyric_browse_hold{};           // 松手后还要保持多久才滑回去（秒）
    int    m_lyric_browse_index{ -1 };      // 上一帧算出的、落在视图中心的那句
    Rect   m_lyric_rect{};                  // 上一帧歌词区的范围，供触摸判定用
    Layout m_layout{};                      // 每帧开头算一次，Update 和 Draw 共用
};
