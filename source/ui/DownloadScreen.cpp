#include "DownloadScreen.h"
#include "Renderer.h"
#include "../Player.h"
#include "../core/FileUtil.h"
#include "../input/InputMap.h"
#include "../input/SoftKeyboard.h"
#include "../net/LyricProvider.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    int VisibleCount()
    {
        int content_height = Theme::kScreenHeight - Theme::kHeaderHeight - Theme::kFooterHeight
                             - Theme::kPadding * 2 - 96;    // 96 是关键词行 + 状态行
        return content_height / Theme::kListItemHeight;
    }
}

void CDownloadScreen::OnEnter(ScreenContext& ctx)
{
    const SongInfo& song = ctx.player->GetCurrentSong();
    // 换歌之后重新生成关键词；同一首歌回到本界面则保留上次的搜索结果
    if (song.file_path != m_song_path)
    {
        m_song_path = song.file_path;
        ResetKeywordFromCurrentSong(ctx);
        ctx.player->GetDownloader().Reset();
        m_selected = 0;
        m_scroll = 0;
        m_scroll_smooth = 0.0;
    }
}

void CDownloadScreen::OnLeave(ScreenContext& ctx)
{
    // 离开界面时不打断已经在跑的下载，让它把文件写完
    (void)ctx;
}

const char* CDownloadScreen::GetButtonHints() const
{
    return "A 下载选中项   X 搜索   Y 切换音乐源   ZL/ZR 歌词/封面开关   B 返回";
}

void CDownloadScreen::ResetKeywordFromCurrentSong(ScreenContext& ctx)
{
    const SongInfo& song = ctx.player->GetCurrentSong();
    m_keyword = LyricProviderUtil::MakeSearchKeyword(
        song.title, song.artist, FileUtil::GetFileNameWithoutExt(song.file_path));
}

CDownloadManager::AutoRequest CDownloadScreen::MakeRequest(ScreenContext& ctx) const
{
    const SongInfo& song = ctx.player->GetCurrentSong();
    CDownloadManager::AutoRequest request;
    request.audio_file_path = song.file_path;
    request.title = song.title;
    request.artist = song.artist;
    request.album = song.album;
    request.download_lyric = m_download_lyric;
    request.download_cover = m_download_cover;
    request.with_translation = ctx.player->GetConfig().GetShowTranslation();
    return request;
}

void CDownloadScreen::StartSearch(ScreenContext& ctx)
{
    CDownloadManager& downloader = ctx.player->GetDownloader();
    if (downloader.IsBusy())
        return;
    if (m_keyword.empty())
    {
        ctx.ShowToast("请先输入搜索关键词");
        return;
    }
    if (!ctx.player->IsNetworkReady())
    {
        ctx.ShowToast("网络不可用");
        return;
    }

    m_selected = 0;
    m_scroll = 0;
    m_scroll_smooth = 0.0;
    downloader.StartSearch(m_keyword);
}

void CDownloadScreen::StartDownload(ScreenContext& ctx, int index)
{
    CDownloadManager& downloader = ctx.player->GetDownloader();
    if (downloader.IsBusy())
        return;

    if (ctx.player->GetCurrentSong().file_path.empty())
    {
        ctx.ShowToast("没有正在播放的曲目");
        return;
    }
    if (!m_download_lyric && !m_download_cover)
    {
        ctx.ShowToast("歌词和封面至少要选一项");
        return;
    }
    if (!downloader.StartDownloadSelected(index, MakeRequest(ctx)))
        ctx.ShowToast("无法开始下载");
}

void CDownloadScreen::EditKeyword(ScreenContext& ctx)
{
    std::string input;
    if (SoftKeyboard::Show("搜索歌曲（歌手 + 歌名）", m_keyword, input))
    {
        m_keyword = input;
        StartSearch(ctx);
    }
}

void CDownloadScreen::EnsureSelectionVisible(int visible_count)
{
    if (m_selected < m_scroll)
        m_scroll = m_selected;
    else if (m_selected >= m_scroll + visible_count)
        m_scroll = m_selected - visible_count + 1;
    m_scroll = std::max(0, m_scroll);
}

void CDownloadScreen::Update(ScreenContext& ctx, double delta_seconds)
{
    CInputMap& input = *ctx.input;
    CDownloadManager& downloader = ctx.player->GetDownloader();
    CDownloadManager::Status status = downloader.Poll();

    m_spinner_phase += delta_seconds * 4.0;

    if (input.IsDown(CInputMap::BTN_B))
    {
        ctx.next_screen = SCREEN_PLAYER;
        return;
    }

    // 下载/搜索进行中时只允许取消，避免并发发起第二个任务
    if (downloader.IsBusy())
    {
        if (input.IsDown(CInputMap::BTN_MINUS))
        {
            downloader.Cancel();
            ctx.ShowToast("已请求取消");
        }
        return;
    }

    if (input.IsDown(CInputMap::BTN_X))
        EditKeyword(ctx);

    if (input.IsDown(CInputMap::BTN_Y))
    {
        CDownloadManager::ProviderId next = downloader.GetProvider() == CDownloadManager::PROVIDER_NETEASE
                                                ? CDownloadManager::PROVIDER_QQ
                                                : CDownloadManager::PROVIDER_NETEASE;
        downloader.SetProvider(next);
        downloader.Reset();
        ctx.ShowToast(std::string("已切换到 ") + CDownloadManager::GetProviderName(next));
        m_selected = 0;
        m_scroll = 0;
        m_scroll_smooth = 0.0;
        return;
    }

    if (input.IsDown(CInputMap::BTN_ZL))
    {
        m_download_lyric = !m_download_lyric;
        ctx.ShowToast(m_download_lyric ? "下载歌词：开" : "下载歌词：关");
    }
    if (input.IsDown(CInputMap::BTN_ZR))
    {
        m_download_cover = !m_download_cover;
        ctx.ShowToast(m_download_cover ? "下载封面：开" : "下载封面：关");
    }

    const int count = static_cast<int>(status.results.size());
    const int visible = VisibleCount();

    if (count == 0)
    {
        // 还没搜过：A 直接触发一次搜索
        if (input.IsDown(CInputMap::BTN_A))
            StartSearch(ctx);
        return;
    }

    if (input.IsRepeat(CInputMap::BTN_DOWN))
        m_selected = (m_selected + 1) % count;
    if (input.IsRepeat(CInputMap::BTN_UP))
        m_selected = (m_selected - 1 + count) % count;
    if (input.IsRepeat(CInputMap::BTN_R))
        m_selected = std::min(count - 1, m_selected + visible);
    if (input.IsRepeat(CInputMap::BTN_L))
        m_selected = std::max(0, m_selected - visible);

    if (input.IsDown(CInputMap::BTN_A))
        StartDownload(ctx, m_selected);

    // 触摸点击直接下载该项
    const CInputMap::TouchState& touch = input.GetTouch();
    if (touch.released && std::abs(touch.delta_y) < 16)
    {
        int list_top = Theme::kHeaderHeight + Theme::kPadding + 96;
        if (touch.y >= list_top && touch.y < list_top + visible * Theme::kListItemHeight)
        {
            int row = (touch.y - list_top) / Theme::kListItemHeight;
            int index = m_scroll + row;
            if (index >= 0 && index < count)
            {
                m_selected = index;
                StartDownload(ctx, index);
            }
        }
    }

    EnsureSelectionVisible(visible);

    double diff = m_scroll - m_scroll_smooth;
    if (std::abs(diff) < 0.01)
        m_scroll_smooth = m_scroll;
    else
        m_scroll_smooth += diff * std::min(1.0, delta_seconds * 18.0);
}

void CDownloadScreen::Draw(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    CDownloadManager& downloader = ctx.player->GetDownloader();
    CDownloadManager::Status status = downloader.Poll();

    const int x = Theme::kPadding * 2;
    const int width = Theme::kScreenWidth - x * 2 - 12;
    const int keyword_y = Theme::kHeaderHeight + Theme::kPadding;
    const int status_y = keyword_y + 44;
    const int list_y = keyword_y + 96;
    const int visible = VisibleCount();

    // ---- 搜索关键词 ----
    r.FillRoundRect(x, keyword_y - 6, width, 40, 6, Theme::kPanel);
    r.DrawText("搜索：", x + 12, keyword_y + 2, CRenderer::FS_SMALL, Theme::kTextDim);
    r.DrawTextEllipsis(m_keyword.empty() ? std::string("（按 X 输入）") : m_keyword,
                       x + 78, keyword_y, width - 90 - 200, CRenderer::FS_NORMAL, Theme::kText);

    // 右侧：音乐源 + 下载项开关
    char toggles[128];
    std::snprintf(toggles, sizeof(toggles), "%s   歌词 %s   封面 %s",
                  downloader.GetProviderName(),
                  m_download_lyric ? "√" : "×",
                  m_download_cover ? "√" : "×");
    r.DrawText(toggles, x + width - 12, keyword_y + 2, CRenderer::FS_SMALL,
               Theme::kAccent, CRenderer::ALIGN_RIGHT);

    // ---- 状态行 ----
    Color status_color = Theme::kTextDim;
    if (status.state == CDownloadManager::ST_FAILED)
        status_color = Theme::kHighlight;
    else if (status.state == CDownloadManager::ST_SUCCESS)
        status_color = Theme::kAccent;

    std::string message = status.message;
    if (downloader.IsBusy())
    {
        // 忙碌时在消息后面转个小圈
        static const char* kSpinner[] = { "|", "/", "-", "\\" };
        int phase = static_cast<int>(m_spinner_phase) % 4;
        message += "  ";
        message += kSpinner[phase];
    }
    r.DrawTextEllipsis(message, x, status_y, width, CRenderer::FS_SMALL, status_color);

    if (!ctx.player->IsNetworkReady())
    {
        r.DrawText("网络未连接", x + width, status_y, CRenderer::FS_SMALL,
                   Theme::kHighlight, CRenderer::ALIGN_RIGHT);
    }
    else if (!ctx.player->IsCertVerified())
    {
        // 没有 CA 证书时连接不做校验，必须让用户知道
        r.DrawText("未验证证书", x + width, status_y, CRenderer::FS_SMALL,
                   Theme::kTextDisabled, CRenderer::ALIGN_RIGHT);
    }

    // ---- 结果列表 ----
    if (status.results.empty())
    {
        const char* hint = downloader.IsBusy() ? "正在请求……" : "按 A 或 X 开始搜索";
        r.DrawText(hint, Theme::kScreenWidth / 2, list_y + 80, CRenderer::FS_NORMAL,
                   Theme::kTextDim, CRenderer::ALIGN_CENTER);
        return;
    }

    r.PushClip(x, list_y, width, visible * Theme::kListItemHeight);

    int first = static_cast<int>(m_scroll_smooth);
    int pixel_offset = static_cast<int>((m_scroll_smooth - first) * Theme::kListItemHeight);

    for (int row = 0; row <= visible; ++row)
    {
        int index = first + row;
        if (index < 0 || index >= static_cast<int>(status.results.size()))
            continue;

        const DownloadItem& item = status.results[index];
        int item_y = list_y + row * Theme::kListItemHeight - pixel_offset;
        bool is_selected = (index == m_selected);

        if (is_selected)
            r.FillRoundRect(x, item_y + 2, width, Theme::kListItemHeight - 4, 6, Theme::kSelection);
        else if (row % 2 == 1)
            r.FillRect(x, item_y, width, Theme::kListItemHeight, Theme::kPanel.WithAlpha(60));

        // 自动匹配选中的那一项加个标记
        if (index == status.matched_index)
            r.FillRoundRect(x + 4, item_y + 12, 4, Theme::kListItemHeight - 24, 2, Theme::kAccent);

        const int time_w = 70;
        const int album_w = 220;
        r.DrawTextEllipsis(item.GetDisplayName(), x + 20, item_y + 14,
                           width - 20 - album_w - time_w - 24, CRenderer::FS_NORMAL, Theme::kText);
        if (!item.album.empty())
        {
            r.DrawTextEllipsis(item.album, x + width - time_w - 16, item_y + 16, album_w,
                               CRenderer::FS_SMALL, Theme::kTextDisabled, CRenderer::ALIGN_RIGHT);
        }
        if (item.duration > 0)
        {
            CPlayTime duration{ item.duration };
            r.DrawText(duration.toString(false), x + width - 12, item_y + 16,
                       CRenderer::FS_SMALL, Theme::kTextDim, CRenderer::ALIGN_RIGHT);
        }
    }

    r.PopClip();

    // 滚动条
    const int track_x = x + width + 4;
    const int track_h = visible * Theme::kListItemHeight;
    r.FillRoundRect(track_x, list_y, 6, track_h, 3, Theme::kPanelAlt);
    if (static_cast<int>(status.results.size()) > visible)
    {
        int thumb_h = std::max(30, track_h * visible / static_cast<int>(status.results.size()));
        int max_scroll = static_cast<int>(status.results.size()) - visible;
        int thumb_y = list_y + static_cast<int>((track_h - thumb_h) * m_scroll_smooth / max_scroll);
        r.FillRoundRect(track_x, thumb_y, 6, thumb_h, 3, Theme::kAccent);
    }
}
