#include "App.h"
#include "Diagnostics.h"
#include "SystemClock.h"
#include "core/FileUtil.h"
#include "core/MediaScanner.h"

#include <switch.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>

bool CApp::Init()
{
    // romfs 用来放图标等资源；没有 romfs 也能跑，所以失败不算致命
    romfsInit();
    SystemClock::Init();

    if (!m_renderer.Init())
    {
        m_last_error = m_renderer.GetLastError();
        return false;
    }
    Diag::ProbeFont(m_renderer);
    if (!m_player.Init())
    {
        m_last_error = m_player.GetLastError();
        return false;
    }
    Diag::ProbeNetwork(m_player.GetHttpClient());

    m_input.Init();
    m_input.SetTouchEnabled(m_player.GetConfig().GetTouchEnabled());

    m_dimmer.Init();
    int dim_timeout = m_player.GetConfig().GetDimTimeout();
    m_dimmer.SetEnabled(dim_timeout > 0);
    if (dim_timeout > 0)
        m_dimmer.SetTimeoutSeconds(dim_timeout);

    m_screens[SCREEN_PLAYER] = &m_player_screen;
    m_screens[SCREEN_PLAYLIST] = &m_playlist_screen;
    m_screens[SCREEN_BROWSER] = &m_browser_screen;
    m_screens[SCREEN_DOWNLOAD] = &m_download_screen;
    m_screens[SCREEN_SETTINGS] = &m_settings_screen;

    m_updater.Init(&m_player.GetHttpClient(), m_self_path);
    m_settings_screen.SetUpdater(&m_updater);

    m_ctx.player = &m_player;
    m_ctx.renderer = &m_renderer;
    m_ctx.input = &m_input;

    RestoreLastSession();

    m_current = SCREEN_PLAYER;
    m_screens[m_current]->OnEnter(m_ctx);
    return true;
}

void CApp::RestoreLastSession()
{
    CConfig& config = m_player.GetConfig();

    std::string last_playlist = config.GetLastPlaylist();
    if (!last_playlist.empty() && FileUtil::Exists(last_playlist))
    {
        // 恢复但不自动播放：开机就出声不是期望行为
        if (m_player.LoadPlaylistFile(last_playlist, false))
        {
            m_player.SelectIndex(config.GetLastIndex());
            return;
        }
    }

    // 没有上次的播放列表：扫一遍默认音乐目录
    std::string music_dir = config.GetMusicDir();
    Diag::Logf("默认音乐目录: %s (是目录=%s)", music_dir.c_str(),
               FileUtil::IsDirectory(music_dir) ? "是" : "否");
    if (!FileUtil::IsDirectory(music_dir))
        return;
    Diag::ProbeDirectory(music_dir);

    std::vector<SongInfo> songs;
    CMediaScanner::ScanDirectory(music_dir, songs, 3);      // 限制 3 层，避免开机卡太久
    Diag::Logf("扫描到 %u 首可播放曲目", static_cast<unsigned>(songs.size()));
    if (!songs.empty())
        m_player.SetPlaylist(std::move(songs), 0, false);
}

void CApp::HandleDownloadResult()
{
    CDownloadManager::Status status = m_player.GetDownloader().Poll();
    if (static_cast<int>(status.state) == m_last_download_state)
        return;
    m_last_download_state = static_cast<int>(status.state);

    if (status.state != CDownloadManager::ST_SUCCESS)
        return;

    // 新歌词写在当前曲目旁边时，立刻重新加载让它生效
    if (!status.saved_lyric_path.empty())
        m_player.ReloadLyric();
    // 封面由 CPlayerScreen 按文件路径缓存，这里让它下一帧重新读取
    if (!status.saved_cover_path.empty())
        m_player_screen.InvalidateCover(m_ctx);

    m_ctx.ShowToast(status.message);
}

void CApp::SwitchScreen(ScreenId id)
{
    if (id < 0 || id >= SCREEN_COUNT || id == m_current)
        return;
    m_screens[m_current]->OnLeave(m_ctx);
    m_current = id;
    m_screens[m_current]->OnEnter(m_ctx);
}

void CApp::DrawHeader()
{
    m_renderer.FillRect(0, 0, Theme::kScreenWidth, Theme::kHeaderHeight, Theme::kPanel);
    m_renderer.DrawLine(0, Theme::kHeaderHeight - 1, Theme::kScreenWidth, Theme::kHeaderHeight - 1,
                        Theme::kSeparator);

    int title_w = 0, title_h = 0;
    m_renderer.MeasureText("MusicPlayer2", CRenderer::FS_LARGE, title_w, title_h);
    m_renderer.DrawText("MusicPlayer2", Theme::kPadding, 20, CRenderer::FS_LARGE, Theme::kAccent);

    // 设置入口做成常驻按钮，比藏在组合键里好找
    {
        int w = 0, h = 0;
        m_renderer.MeasureText("设置", CRenderer::FS_SMALL, w, h);
        m_settings_button.w = w + 20;
        m_settings_button.h = h + 8;
        m_settings_button.x = Theme::kPadding + title_w + 20;
        m_settings_button.y = 26;
        m_renderer.FillRoundRect(m_settings_button.x, m_settings_button.y, m_settings_button.w,
                                 m_settings_button.h, 6, Theme::kPanelAlt);
        m_renderer.DrawText("设置", m_settings_button.x + 10, m_settings_button.y + 4,
                            CRenderer::FS_SMALL, Theme::kText);
    }
    m_renderer.DrawText(m_screens[m_current]->GetTitle(), Theme::kScreenWidth / 2, 22,
                        CRenderer::FS_NORMAL, Theme::kText, CRenderer::ALIGN_CENTER);

    // 右上角分两行：上面是日期时间，下面是播放模式和音量
    const int right_x = Theme::kScreenWidth - Theme::kPadding;

    SystemClock::DateTime now = SystemClock::Now();
    std::string clock_text = SystemClock::FormatDate(now) + "  " + SystemClock::FormatTime(now);
    m_renderer.DrawText(clock_text, right_x, 8, CRenderer::FS_SMALL,
                        now.valid ? Theme::kText : Theme::kTextDisabled, CRenderer::ALIGN_RIGHT);

    // 第二行从右往左依次是：音量、播放模式按钮、触摸开关按钮
    char volume_text[32];
    std::snprintf(volume_text, sizeof(volume_text), "音量 %d%%", m_player.GetVolume());
    int volume_w = 0, volume_h = 0;
    m_renderer.MeasureText(volume_text, CRenderer::FS_SMALL, volume_w, volume_h);
    m_renderer.DrawText(volume_text, right_x, 38, CRenderer::FS_SMALL, Theme::kTextDim,
                        CRenderer::ALIGN_RIGHT);

    const int button_pad = 10;
    const int button_y = 34;
    int cursor_x = right_x - volume_w - 16;         // 从右往左排布的游标

    // 播放模式按钮
    const char* mode_text = CPlayer::GetRepeatModeName(m_player.GetRepeatMode());
    int mode_w = 0, mode_h = 0;
    m_renderer.MeasureText(mode_text, CRenderer::FS_SMALL, mode_w, mode_h);
    m_repeat_button.w = mode_w + button_pad * 2;
    m_repeat_button.h = mode_h + 8;
    m_repeat_button.x = cursor_x - m_repeat_button.w;
    m_repeat_button.y = button_y;
    m_renderer.FillRoundRect(m_repeat_button.x, m_repeat_button.y, m_repeat_button.w,
                             m_repeat_button.h, 6, Theme::kPanelAlt);
    m_renderer.DrawText(mode_text, m_repeat_button.x + button_pad, m_repeat_button.y + 4,
                        CRenderer::FS_SMALL, Theme::kText);
    cursor_x = m_repeat_button.x - 8;

    // 触摸开关按钮
    const bool touch_on = m_input.IsTouchEnabled();
    const char* touch_text = touch_on ? "触摸 开" : "触摸 关";
    int touch_w = 0, touch_h = 0;
    m_renderer.MeasureText(touch_text, CRenderer::FS_SMALL, touch_w, touch_h);
    m_touch_button.w = touch_w + button_pad * 2;
    m_touch_button.h = touch_h + 8;
    m_touch_button.x = cursor_x - m_touch_button.w;
    m_touch_button.y = button_y;
    m_renderer.FillRoundRect(m_touch_button.x, m_touch_button.y, m_touch_button.w,
                             m_touch_button.h, 6,
                             touch_on ? Theme::kPanelAlt : Theme::kAccentDim);
    m_renderer.DrawText(touch_text, m_touch_button.x + button_pad, m_touch_button.y + 4,
                        CRenderer::FS_SMALL, touch_on ? Theme::kText : Theme::kTextDim);
}

void CApp::HandleHeaderTouch()
{
    // 触摸开关按钮必须用原始触摸状态：走 GetTouch() 的话，
    // 关掉触摸之后这个按钮自己也失效了，就再也开不回来
    const CInputMap::TouchState& raw = m_input.GetRawTouch();
    // 划动不算点击；矩形来自上一帧的 DrawHeader，首帧为空不会误命中
    if (!raw.released || raw.IsDrag())
        return;

    if (m_touch_button.Contains(raw.x, raw.y))
    {
        bool enabled = !m_input.IsTouchEnabled();
        m_input.SetTouchEnabled(enabled);
        m_player.GetConfig().SetTouchEnabled(enabled);
        m_ctx.ShowToast(enabled ? "已启用触摸操作" : "已禁用触摸操作");
        return;
    }

    // 其余顶栏按钮遵守触摸开关
    if (!m_input.IsTouchEnabled())
        return;

    if (m_repeat_button.Contains(raw.x, raw.y))
    {
        m_player.SwitchRepeatMode();
        m_ctx.ShowToast(CPlayer::GetRepeatModeName(m_player.GetRepeatMode()));
    }
    else if (m_settings_button.Contains(raw.x, raw.y))
    {
        m_ctx.next_screen = SCREEN_SETTINGS;
    }
}

void CApp::DrawDimOverlay()
{
    if (!m_dimmer.IsDimmed())
        return;
    // 背光已经调暗了，这里再压一层是为了让"省电中"这个状态一眼可辨
    m_renderer.FillRect(0, 0, Theme::kScreenWidth, Theme::kScreenHeight, Color{ 0, 0, 0, 150 });
    m_renderer.DrawText("省电模式 · 按任意键唤醒", Theme::kScreenWidth / 2,
                        Theme::kScreenHeight - 120, CRenderer::FS_SMALL, Theme::kTextDim,
                        CRenderer::ALIGN_CENTER);
}

void CApp::DrawFooter()
{
    const int y = Theme::kScreenHeight - Theme::kFooterHeight;
    m_renderer.FillRect(0, y, Theme::kScreenWidth, Theme::kFooterHeight, Theme::kPanel);
    m_renderer.DrawLine(0, y, Theme::kScreenWidth, y, Theme::kSeparator);

    m_renderer.DrawTextEllipsis(m_screens[m_current]->GetButtonHints(), Theme::kPadding, y + 20,
                                Theme::kScreenWidth - Theme::kPadding * 2,
                                CRenderer::FS_SMALL, Theme::kTextDim);
}

void CApp::DrawToast(double delta_seconds)
{
    if (m_ctx.toast_timer <= 0.0)
        return;
    m_ctx.toast_timer -= delta_seconds;

    int text_w = 0, text_h = 0;
    m_renderer.MeasureText(m_ctx.toast_text, CRenderer::FS_NORMAL, text_w, text_h);

    const int padding = 24;
    const int w = text_w + padding * 2;
    const int h = text_h + 20;
    const int x = (Theme::kScreenWidth - w) / 2;
    const int y = Theme::kHeaderHeight + 24;

    uint8_t alpha = static_cast<uint8_t>(std::min(1.0, m_ctx.toast_timer / 0.4) * 235);
    m_renderer.FillRoundRect(x, y, w, h, 8, Theme::kAccentDim.WithAlpha(alpha));
    m_renderer.DrawText(m_ctx.toast_text, Theme::kScreenWidth / 2, y + 10, CRenderer::FS_NORMAL,
                        Theme::kText.WithAlpha(alpha), CRenderer::ALIGN_CENTER);
}

void CApp::Run()
{
    m_running = true;
    uint32_t last_ticks = SDL_GetTicks();

    while (m_running)
    {
        uint32_t now = SDL_GetTicks();
        double delta_seconds = (now - last_ticks) / 1000.0;
        last_ticks = now;
        // 掉帧时钳住 delta，避免动画和快进步长瞬间跳一大截
        delta_seconds = std::min(delta_seconds, 0.1);

        // SDL 事件仍要抽干，否则窗口/退出事件会堆积
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                m_running = false;
        }

        m_input.Update(delta_seconds);
        if (m_input.ShouldExit())
            m_running = false;

        // 设置界面可能改了变暗档位，每帧同步一次
        int dim_timeout = m_player.GetConfig().GetDimTimeout();
        m_dimmer.SetEnabled(dim_timeout > 0);
        if (dim_timeout > 0)
            m_dimmer.SetTimeoutSeconds(dim_timeout);

        // 只在播放中才自动调暗：暂停着还熄屏，多半是用户正在挑歌
        m_dimmer.Update(delta_seconds, m_input.HasAnyInput(), m_player.IsPlaying());

        m_player.Update();
        m_player.GetAudio().GetSpectrum().Update(delta_seconds);
        HandleDownloadResult();

        m_ctx.next_screen = SCREEN_NONE;
        m_screens[m_current]->Update(m_ctx, delta_seconds);
        // 放在界面 Update 之后：顶栏按钮的优先级更高，而且要避免
        // 上面那句 next_screen = SCREEN_NONE 把顶栏设的跳转清掉
        HandleHeaderTouch();
        if (m_ctx.request_exit)
            m_running = false;

        m_renderer.BeginFrame();
        m_renderer.Clear(Theme::kBackground);
        m_screens[m_current]->Draw(m_ctx);
        DrawHeader();
        DrawFooter();
        DrawToast(delta_seconds);
        DrawDimOverlay();
        m_renderer.EndFrame();
        m_renderer.TrimCache();

        // 界面切换放在绘制之后，保证本帧画的是同一个界面
        if (m_ctx.next_screen != SCREEN_NONE)
            SwitchScreen(m_ctx.next_screen);
    }
}

void CApp::Uninit()
{
    // 先让界面交还纹理，再销毁渲染器
    for (CScreen* screen : m_screens)
    {
        if (screen != nullptr)
            screen->ReleaseResources(m_ctx);
    }

    // 更新线程可能正在用 curl，必须在 CPlayer 拆掉网络栈之前收掉
    m_updater.WaitForCompletion();
    // 亮度是全局设置，退出前一定要还原
    m_dimmer.Uninit();
    m_player.Uninit();
    m_renderer.Uninit();
    SystemClock::Uninit();
    romfsExit();
}
