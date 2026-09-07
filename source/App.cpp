#include "App.h"
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

    if (!m_renderer.Init())
    {
        m_last_error = m_renderer.GetLastError();
        return false;
    }
    if (!m_player.Init())
    {
        m_last_error = m_player.GetLastError();
        return false;
    }
    m_input.Init();

    m_screens[SCREEN_PLAYER] = &m_player_screen;
    m_screens[SCREEN_PLAYLIST] = &m_playlist_screen;
    m_screens[SCREEN_BROWSER] = &m_browser_screen;
    m_screens[SCREEN_DOWNLOAD] = &m_download_screen;

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
    if (!FileUtil::IsDirectory(music_dir))
        return;

    std::vector<SongInfo> songs;
    CMediaScanner::ScanDirectory(music_dir, songs, 3);      // 限制 3 层，避免开机卡太久
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

    m_renderer.DrawText("MusicPlayer2", Theme::kPadding, 20, CRenderer::FS_LARGE, Theme::kAccent);
    m_renderer.DrawText(m_screens[m_current]->GetTitle(), Theme::kScreenWidth / 2, 22,
                        CRenderer::FS_NORMAL, Theme::kText, CRenderer::ALIGN_CENTER);

    // 右上角：播放模式 + 音量
    char status[96];
    std::snprintf(status, sizeof(status), "%s   音量 %d%%",
                  CPlayer::GetRepeatModeName(m_player.GetRepeatMode()), m_player.GetVolume());
    m_renderer.DrawText(status, Theme::kScreenWidth - Theme::kPadding, 26,
                        CRenderer::FS_SMALL, Theme::kTextDim, CRenderer::ALIGN_RIGHT);
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

        m_player.Update();
        m_player.GetAudio().GetSpectrum().Update(delta_seconds);
        HandleDownloadResult();

        m_ctx.next_screen = SCREEN_NONE;
        m_screens[m_current]->Update(m_ctx, delta_seconds);
        if (m_ctx.request_exit)
            m_running = false;

        m_renderer.BeginFrame();
        m_renderer.Clear(Theme::kBackground);
        m_screens[m_current]->Draw(m_ctx);
        DrawHeader();
        DrawFooter();
        DrawToast(delta_seconds);
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

    m_player.Uninit();
    m_renderer.Uninit();
    romfsExit();
}
