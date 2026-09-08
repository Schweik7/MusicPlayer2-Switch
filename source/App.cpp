#include "App.h"
#include "Diagnostics.h"
#include "SystemClock.h"
#include "core/FileUtil.h"
#include "core/MediaScanner.h"
#include "ui/Theme.h"

#include <switch.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

void CApp::BootStage(const char* name)
{
    const uint32_t now = SDL_GetTicks();
    m_boot_timings.push_back(BootStageTime{ name, now - m_boot_last_ticks });
    m_boot_last_ticks = now;
    Diag::Logf("启动阶段 %-12s %u 毫秒", name, m_boot_timings.back().ms);

    // 渲染器一旦就绪就把进度画出来。在此之前只能记时间，画不了东西。
    if (!m_renderer.IsReady())
        return;
    m_renderer.BeginFrame();
    m_renderer.Clear(Theme::kBackground);
    m_renderer.DrawText("MusicPlayer2", Theme::kScreenWidth / 2, Theme::kScreenHeight / 2 - 60,
                        CRenderer::FS_LARGE, Theme::kText, CRenderer::ALIGN_CENTER);
    m_renderer.DrawText(std::string("正在启动 · ") + name, Theme::kScreenWidth / 2,
                        Theme::kScreenHeight / 2 + 10, CRenderer::FS_SMALL, Theme::kTextDim,
                        CRenderer::ALIGN_CENTER);
    m_renderer.EndFrame();
}

bool CApp::Init()
{
    m_boot_start_ticks = SDL_GetTicks();
    m_boot_last_ticks = m_boot_start_ticks;

    // 应用待装的更新——必须赶在 romfsInit() 之前。
    //
    // romfs 是从正在运行的这个 NRO 文件里挂载的，挂着的时候整个文件被 FS 层持有：
    // 删不掉、改不了名、也打不开写。之前"更新已就绪但写不进去"就是这么来的，
    // 当时这段代码排在 romfsInit() 后面。
    // 顺序换过来，此刻文件还没被任何东西按住，一个 rename 就换完了。
    m_updater.SetSelfPath(m_self_path);
    m_pending_update_note = m_updater.ApplyPendingUpdate();

    // romfs 用来放 CA 证书等资源；没有 romfs 也能跑，所以失败不算致命
    romfsInit();
    SystemClock::Init();
    BootStage("romfs");

    // 用户可以往数据目录里放一个 font.ttf 换掉界面字体（系统字体仍作兜底）。
    // 不打包进 NRO：漂亮的中文字体动辄二三十兆，而 NRO 是整个读进内存才启动的，
    // 打进去等于给每次冷启动加上几秒黑屏，而这恰恰是想避免的事。
    std::string custom_font = FileUtil::Combine(CPlayer::GetDataDir(), "font.ttf");
    if (!FileUtil::Exists(custom_font))
        custom_font.clear();

    if (!m_renderer.Init(custom_font))
    {
        m_last_error = m_renderer.GetLastError();
        return false;
    }
    BootStage(custom_font.empty() ? "图形与字体" : "图形与字体(自带)");

    Diag::ProbeFont(m_renderer);
    if (!m_player.Init())
    {
        m_last_error = m_player.GetLastError();
        return false;
    }
    BootStage("音频与网络");

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
    m_settings_screen.SetApp(this);

    m_ctx.player = &m_player;
    m_ctx.renderer = &m_renderer;
    m_ctx.input = &m_input;
    BootStage("子系统");

    RestoreLastSession();
    BootStage("恢复上次会话");

    m_current = SCREEN_PLAYER;
    m_screens[m_current]->OnEnter(m_ctx);
    if (!m_pending_update_note.empty())
        m_ctx.ShowToast(m_pending_update_note);

    m_boot_timings.push_back(BootStageTime{ "合计", SDL_GetTicks() - m_boot_start_ticks });
    Diag::Logf("启动合计 %u 毫秒", m_boot_timings.back().ms);
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

    // 没有上次的播放列表：扫默认音乐目录。
    // 扫描要逐个文件读标签，几百首要十秒左右，绝不能挡在启动路径上，
    // 所以丢给后台线程，界面先起来，扫完再把结果接进去。
    std::string music_dir = config.GetMusicDir();
    Diag::Logf("默认音乐目录: %s (是目录=%s)", music_dir.c_str(),
               FileUtil::IsDirectory(music_dir) ? "是" : "否");
    if (!FileUtil::IsDirectory(music_dir))
        return;
    Diag::ProbeDirectory(music_dir);

    m_scan_start_ticks = SDL_GetTicks();
    m_scanner.Start(music_dir, 3);          // 限制 3 层，避免在很深的目录树里空转
}

void CApp::HandleScanResult()
{
    std::vector<SongInfo> songs;
    if (!m_scanner.TakeResult(songs))
        return;

    Diag::Logf("后台扫描完成：%u 首，耗时 %u 毫秒",
               static_cast<unsigned>(songs.size()), SDL_GetTicks() - m_scan_start_ticks);

    if (songs.empty())
        return;
    // 扫描期间用户可能已经自己选了目录或播放列表，那就别覆盖他的选择
    if (!m_player.IsPlaylistEmpty())
    {
        Diag::Logf("播放列表已有内容，丢弃后台扫描结果");
        return;
    }

    int count = static_cast<int>(songs.size());
    m_player.SetPlaylist(std::move(songs), 0, false);
    m_player.SelectIndex(m_player.GetConfig().GetLastIndex());

    char message[64];
    std::snprintf(message, sizeof(message), "音乐库扫描完成，共 %d 首", count);
    m_ctx.ShowToast(message);
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

namespace
{
    // 顶栏上一个小按钮：画出来并把矩形交回去，绘制和命中判定共用同一份坐标
    Rect DrawChip(CRenderer& renderer, const std::string& text, int x, int y, Color background,
                  Color foreground)
    {
        const int pad = 10;
        int w = 0, h = 0;
        renderer.MeasureText(text, CRenderer::FS_SMALL, w, h);
        Rect rect{ x, y, w + pad * 2, h + 8 };
        renderer.FillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, background);
        renderer.DrawText(text, rect.x + pad, rect.y + 4, CRenderer::FS_SMALL, foreground);
        return rect;
    }
}

void CApp::ClearHeaderButtons()
{
    m_back_button = Rect{};
    m_touch_button = Rect{};
    m_settings_button = Rect{};
    m_playlist_button = Rect{};
}

void CApp::DrawHeader()
{
    m_renderer.FillRect(0, 0, Theme::kScreenWidth, Theme::kHeaderHeight, Theme::kPanel);
    m_renderer.DrawLine(0, Theme::kHeaderHeight - 1, Theme::kScreenWidth, Theme::kHeaderHeight - 1,
                        Theme::kSeparator);

    // 标题左边缘和左栏封面对齐（封面居中于 48..308 的左栏里）。
    // 返回按钮收成一个窄箭头，正好塞在标题左边那块空当里。
    const int title_x = 78;

    m_back_button = Rect{};
    if (m_screens[m_current]->CanGoBack())
    {
        m_back_button = Rect{ Theme::kPadding, 22, 42, 30 };
        m_renderer.FillRoundRect(m_back_button.x, m_back_button.y, m_back_button.w,
                                 m_back_button.h, 6, Theme::kPanelAlt);
        m_renderer.DrawText("←", m_back_button.x + m_back_button.w / 2, m_back_button.y + 3,
                            CRenderer::FS_NORMAL, Theme::kText, CRenderer::ALIGN_CENTER);
    }

    int title_w = 0, title_h = 0;
    m_renderer.MeasureText("MusicPlayer2", CRenderer::FS_LARGE, title_w, title_h);
    m_renderer.DrawText("MusicPlayer2", title_x, 20, CRenderer::FS_LARGE, Theme::kAccent);

    // 设置和播放列表做成常驻按钮，比藏在 ＋ / − 键里好找，
    // 也让纯触摸操作能进得去
    int left_cursor = title_x + title_w + 16;
    m_settings_button = DrawChip(m_renderer, "设置", left_cursor, 26, Theme::kPanelAlt,
                                 Theme::kText);
    left_cursor = m_settings_button.x + m_settings_button.w + 8;
    m_playlist_button = DrawChip(m_renderer, "列表", left_cursor, 26, Theme::kPanelAlt,
                                 Theme::kText);

    // 第几首 / 共几首。放顶栏而不是播放界面里：它在哪个界面都有意义，
    // 而且沉浸模式下播放界面上的东西是要收起来的。
    const int total = m_player.GetPlaylistSize();
    if (total > 0)
    {
        char fraction[32];
        std::snprintf(fraction, sizeof(fraction), "%d / %d",
                      m_player.GetCurrentIndex() + 1, total);
        m_renderer.DrawText(fraction, m_playlist_button.x + m_playlist_button.w + 14, 30,
                            CRenderer::FS_SMALL, Theme::kAccent);
    }

    // 后台扫描时把状态顶到中间：否则用户会以为"打开就是空列表"
    CLibraryScanner::Progress scan = m_scanner.Poll();
    if (scan.running)
    {
        m_renderer.DrawText("正在扫描音乐库…", Theme::kScreenWidth / 2, 22,
                            CRenderer::FS_NORMAL, Theme::kHighlight, CRenderer::ALIGN_CENTER);
    }
    else
    {
        m_renderer.DrawText(m_screens[m_current]->GetTitle(), Theme::kScreenWidth / 2, 22,
                            CRenderer::FS_NORMAL, Theme::kText, CRenderer::ALIGN_CENTER);
    }

    // 右上角一行：触摸开关 + 日期时间。
    //
    // 原来是两行（上面时间、下面几个按钮），播放模式按钮挪到歌词区的工具排之后
    // 下面那行只剩一个开关，两行显得空。并成一行，顶栏也清爽。
    const int right_x = Theme::kScreenWidth - Theme::kPadding;

    SystemClock::DateTime now = SystemClock::Now();
    std::string clock_text = SystemClock::FormatDate(now) + "  " + SystemClock::FormatTime(now);
    int clock_w = 0, clock_h = 0;
    m_renderer.MeasureText(clock_text, CRenderer::FS_SMALL, clock_w, clock_h);
    m_renderer.DrawText(clock_text, right_x, 26, CRenderer::FS_SMALL,
                        now.valid ? Theme::kText : Theme::kTextDisabled, CRenderer::ALIGN_RIGHT);

    const bool touch_on = m_input.IsTouchEnabled();
    const std::string touch_text = touch_on ? "触摸 开" : "触摸 关";
    int touch_w = 0, touch_h = 0;
    m_renderer.MeasureText(touch_text, CRenderer::FS_SMALL, touch_w, touch_h);
    m_touch_button = DrawChip(m_renderer, touch_text,
                              right_x - clock_w - 16 - (touch_w + 20), 22,
                              touch_on ? Theme::kPanelAlt : Theme::kAccentDim,
                              touch_on ? Theme::kText : Theme::kTextDim);
}

void CApp::ToggleTouchEnabled()
{
    bool enabled = !m_input.IsTouchEnabled();
    m_input.SetTouchEnabled(enabled);
    m_player.GetConfig().SetTouchEnabled(enabled);
    // 关掉触摸就是沉浸模式：屏上的按钮全收起来，封面和歌词占满。
    // 必须把"怎么退出"说清楚——收起来之后连那个开关本身都不在屏幕上了。
    m_ctx.ShowToast(enabled ? "已启用触摸操作"
                            : "沉浸模式：触摸已关闭，按下左摇杆（LS）恢复");
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
        ToggleTouchEnabled();
        return;
    }

    // 其余顶栏按钮遵守触摸开关
    if (!m_input.IsTouchEnabled())
        return;

    // 播放模式的按钮挪到了播放界面歌词区的工具排里：
    // 那里能同时把当前模式画出来，顶栏只剩一个文字标签反而占地方。
    if (m_settings_button.Contains(raw.x, raw.y))
    {
        m_ctx.next_screen = SCREEN_SETTINGS;
    }
    else if (m_playlist_button.Contains(raw.x, raw.y))
    {
        m_ctx.next_screen = SCREEN_PLAYLIST;
    }
    else if (m_back_button.Contains(raw.x, raw.y))
    {
        // 走界面自己的 GoBack，和按 B 是同一条路径，两者不会走偏
        m_screens[m_current]->GoBack(m_ctx);
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
    if (m_player.GetConfig().GetHideHints())
        return;

    const int y = Theme::kScreenHeight - Theme::kFooterHeight;
    m_renderer.FillRect(0, y, Theme::kScreenWidth, Theme::kFooterHeight, Theme::kPanel);
    m_renderer.DrawLine(0, y, Theme::kScreenWidth, y, Theme::kSeparator);

    // 提示串用 '|' 分段，这里把每段放进等宽的一格里居中。
    // 原来是一整行左对齐，长短不一的几段挤在左边、右边空一大片，很难扫读。
    const std::string hints = m_screens[m_current]->GetButtonHints();
    std::vector<std::string> parts;
    for (size_t start = 0; start <= hints.size(); )
    {
        size_t sep = hints.find('|', start);
        if (sep == std::string::npos)
            sep = hints.size();
        std::string part = hints.substr(start, sep - start);
        if (!part.empty())
            parts.push_back(part);
        start = sep + 1;
    }
    if (parts.empty())
        return;

    // 按每段自己的宽度排，把富余的横向空间平分成等宽的间隙。
    //
    // 原来是给每段分一个等宽的格子再居中：长的那段（"右摇杆↑↓ 翻歌词"）被省略号
    // 截断，短的那两段（"− 列表" "＋ 设置"）左右却空一大片。
    const int usable = Theme::kScreenWidth - Theme::kPadding * 2;
    std::vector<int> widths(parts.size(), 0);
    int total_text = 0;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        int h = 0;
        m_renderer.MeasureText(parts[i], CRenderer::FS_SMALL, widths[i], h);
        total_text += widths[i];
    }

    if (total_text > usable)
    {
        // 实在放不下就退回等宽格子，让每段均摊被截断的代价
        const int slot = usable / static_cast<int>(parts.size());
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const int center = Theme::kPadding + static_cast<int>(i) * slot + slot / 2;
            m_renderer.DrawTextEllipsis(parts[i], center, y + 20, slot - 8, CRenderer::FS_SMALL,
                                        Theme::kTextDim, CRenderer::ALIGN_CENTER);
        }
        return;
    }

    // 段与段之间、以及两端，都留同样宽的间隙
    const int gap = (usable - total_text) / (static_cast<int>(parts.size()) + 1);
    int x = Theme::kPadding + gap;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        m_renderer.DrawText(parts[i], x, y + 20, CRenderer::FS_SMALL, Theme::kTextDim);
        x += widths[i] + gap;
    }
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
        HandleScanResult();
        m_player.GetAudio().GetSpectrum().Update(delta_seconds);
        HandleDownloadResult();

        m_ctx.next_screen = SCREEN_NONE;
        m_screens[m_current]->Update(m_ctx, delta_seconds);
        // 放在界面 Update 之后：顶栏按钮的优先级更高，而且要避免
        // 上面那句 next_screen = SCREEN_NONE 把顶栏设的跳转清掉
        HandleHeaderTouch();
        // 按下左摇杆开关触摸，任何界面下都有效。
        // 误触多发生在手持时，这个键刚好在拇指边上
        if (m_input.IsDown(CInputMap::BTN_STICK_L))
            ToggleTouchEnabled();
        if (m_ctx.request_exit)
            m_running = false;

        m_renderer.AdvanceTime(delta_seconds);
        m_renderer.BeginFrame();
        m_renderer.Clear(Theme::kBackground);
        m_screens[m_current]->Draw(m_ctx);
        // 封面全屏这类独占画面不画顶栏底栏。
        // 顶栏的按钮矩形也要一并清掉，否则点在图上会命中上一帧留下的位置。
        if (m_screens[m_current]->WantsFullScreen())
            ClearHeaderButtons();
        else
        {
            DrawHeader();
            DrawFooter();
        }
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

    // 后台线程都要在各自用到的资源被拆掉之前收掉
    m_scanner.WaitForCompletion();
    // 更新线程可能正在用 curl，必须在 CPlayer 拆掉网络栈之前收掉
    m_updater.WaitForCompletion();
    // 亮度是全局设置，退出前一定要还原
    m_dimmer.Uninit();
    m_player.Uninit();
    m_renderer.Uninit();
    SystemClock::Uninit();
    romfsExit();
}
