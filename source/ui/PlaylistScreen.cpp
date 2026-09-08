#include "PlaylistScreen.h"
#include "ListScroller.h"
#include "Renderer.h"
#include "../Player.h"
#include "../input/InputMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    int VisibleCount()
    {
        int content_height = Theme::kScreenHeight - Theme::kHeaderHeight - Theme::kFooterHeight
                             - Theme::kPadding * 2;
        return content_height / Theme::kListItemHeight;
    }
}

void CPlaylistScreen::OnEnter(ScreenContext& ctx)
{
    // 进入时把光标定位到正在播放的曲目
    int current = ctx.player->GetCurrentIndex();
    if (current >= 0)
        m_selected = current;
    m_selected = std::max(0, std::min(m_selected, ctx.player->GetPlaylistSize() - 1));
    EnsureSelectionVisible(VisibleCount());
    m_scroll_smooth = m_scroll;
}

const char* CPlaylistScreen::GetButtonHints() const
{
    return "A 播放选中项|B 返回|L/R 翻页|＋ 设置";
}

bool CPlaylistScreen::UpdateTouchScroll(ScreenContext& ctx, int count, int visible,
                                        double delta_seconds)
{
    ListScroller::Params params;
    params.list_top = Theme::kHeaderHeight + Theme::kPadding;
    params.item_height = Theme::kListItemHeight;
    params.count = count;
    params.visible = visible;
    return ListScroller::Update(ctx.input->GetTouch(), params, delta_seconds,
                                m_scroll, m_scroll_smooth, m_dragging, m_fling);
}

void CPlaylistScreen::EnsureSelectionVisible(int visible_count)
{
    if (m_selected < m_scroll)
        m_scroll = m_selected;
    else if (m_selected >= m_scroll + visible_count)
        m_scroll = m_selected - visible_count + 1;
    m_scroll = std::max(0, m_scroll);
}

void CPlaylistScreen::Update(ScreenContext& ctx, double delta_seconds)
{
    CPlayer& player = *ctx.player;
    CInputMap& input = *ctx.input;
    const int count = player.GetPlaylistSize();
    const int visible = VisibleCount();

    if (input.IsDown(CInputMap::BTN_B) || input.IsDown(CInputMap::BTN_MINUS))
    {
        ctx.next_screen = SCREEN_PLAYER;
        return;
    }
    if (input.IsDown(CInputMap::BTN_PLUS))
    {
        ctx.next_screen = SCREEN_SETTINGS;
        return;
    }

    if (count == 0)
        return;

    if (input.IsRepeat(CInputMap::BTN_DOWN))
        m_selected = (m_selected + 1) % count;
    if (input.IsRepeat(CInputMap::BTN_UP))
        m_selected = (m_selected - 1 + count) % count;
    if (input.IsRepeat(CInputMap::BTN_R))
        m_selected = std::min(count - 1, m_selected + visible);
    if (input.IsRepeat(CInputMap::BTN_L))
        m_selected = std::max(0, m_selected - visible);

    if (input.IsDown(CInputMap::BTN_A))
    {
        if (player.PlayIndex(m_selected))
            ctx.next_screen = SCREEN_PLAYER;
        else
            ctx.ShowToast("无法播放该文件");
    }

    // 触摸拖动滚动（含松手惯性）
    bool touch_scrolled = UpdateTouchScroll(ctx, count, visible, delta_seconds);

    // 触摸：点击某一行直接播放。划动过就不算点击，避免滑列表时误播放
    const CInputMap::TouchState& touch = input.GetTouch();
    if (touch.released && !touch.IsDrag())
    {
        int list_top = Theme::kHeaderHeight + Theme::kPadding;
        if (touch.y >= list_top && touch.y < list_top + visible * Theme::kListItemHeight)
        {
            int row = (touch.y - list_top) / Theme::kListItemHeight;
            int index = m_scroll + row;
            if (index >= 0 && index < count)
            {
                m_selected = index;
                if (player.PlayIndex(index))
                    ctx.next_screen = SCREEN_PLAYER;
            }
        }
    }

    if (touch_scrolled)
    {
        // 触摸在主导滚动：把光标拉进可见范围，而不是反过来把列表拽回光标处
        m_selected = std::max(m_scroll, std::min(m_selected, m_scroll + visible - 1));
        m_selected = std::max(0, std::min(m_selected, count - 1));
        return;
    }

    EnsureSelectionVisible(visible);

    // 滚动位置向目标值平滑逼近
    double diff = m_scroll - m_scroll_smooth;
    if (std::abs(diff) < 0.01)
        m_scroll_smooth = m_scroll;
    else
        m_scroll_smooth += diff * std::min(1.0, delta_seconds * 18.0);
}

void CPlaylistScreen::Draw(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    CPlayer& player = *ctx.player;
    const std::vector<SongInfo>& songs = player.GetPlaylist();

    const int list_x = Theme::kPadding * 2;
    const int list_y = Theme::kHeaderHeight + Theme::kPadding;
    const int list_w = Theme::kScreenWidth - list_x * 2 - 12;    // 右侧留出滚动条
    const int visible = VisibleCount();

    if (songs.empty())
    {
        r.DrawText("播放列表为空", Theme::kScreenWidth / 2, list_y + 120, CRenderer::FS_LARGE,
                   Theme::kTextDim, CRenderer::ALIGN_CENTER);
        r.DrawText("按 + 从 SD 卡添加音乐", Theme::kScreenWidth / 2, list_y + 170,
                   CRenderer::FS_NORMAL, Theme::kTextDisabled, CRenderer::ALIGN_CENTER);
        return;
    }

    r.PushClip(list_x, list_y, list_w, visible * Theme::kListItemHeight);

    // 多画一行，配合平滑滚动时的半行位移
    int first = static_cast<int>(m_scroll_smooth);
    int pixel_offset = static_cast<int>((m_scroll_smooth - first) * Theme::kListItemHeight);

    for (int row = 0; row <= visible; ++row)
    {
        int index = first + row;
        if (index < 0 || index >= static_cast<int>(songs.size()))
            continue;

        const SongInfo& song = songs[index];
        int item_y = list_y + row * Theme::kListItemHeight - pixel_offset;
        bool is_selected = (index == m_selected);
        bool is_playing = (index == player.GetCurrentIndex());

        if (is_selected)
            r.FillRoundRect(list_x, item_y + 2, list_w, Theme::kListItemHeight - 4, 6, Theme::kSelection);
        else if (row % 2 == 1)
            r.FillRect(list_x, item_y, list_w, Theme::kListItemHeight, Theme::kPanel.WithAlpha(60));

        // 正在播放的曲目左侧加一个指示条
        if (is_playing)
        {
            r.FillRoundRect(list_x + 4, item_y + 12, 4, Theme::kListItemHeight - 24, 2, Theme::kAccent);
        }

        char number[16];
        std::snprintf(number, sizeof(number), "%d", index + 1);
        r.DrawText(number, list_x + 58, item_y + 16, CRenderer::FS_SMALL,
                   Theme::kTextDisabled, CRenderer::ALIGN_RIGHT);

        Color title_color = is_playing ? Theme::kAccent : Theme::kText;
        int text_x = list_x + 74;
        int time_w = 70;
        r.DrawTextEllipsis(song.GetDisplayName(), text_x, item_y + 14, list_w - 74 - time_w - 16,
                           CRenderer::FS_NORMAL, title_color);

        if (song.length.toInt() > 0)
        {
            r.DrawText(song.length.toString(false), list_x + list_w - 12, item_y + 16,
                       CRenderer::FS_SMALL, Theme::kTextDim, CRenderer::ALIGN_RIGHT);
        }
    }

    r.PopClip();

    // 滚动条
    const int track_x = list_x + list_w + 4;
    const int track_h = visible * Theme::kListItemHeight;
    r.FillRoundRect(track_x, list_y, 6, track_h, 3, Theme::kPanelAlt);
    if (static_cast<int>(songs.size()) > visible)
    {
        int thumb_h = std::max(30, track_h * visible / static_cast<int>(songs.size()));
        int max_scroll = static_cast<int>(songs.size()) - visible;
        int thumb_y = list_y + static_cast<int>((track_h - thumb_h) * m_scroll_smooth / max_scroll);
        r.FillRoundRect(track_x, thumb_y, 6, thumb_h, 3, Theme::kAccent);
    }

    // 底部统计
    char summary[96];
    std::snprintf(summary, sizeof(summary), "%d / %d", m_selected + 1, static_cast<int>(songs.size()));
    r.DrawText(summary, Theme::kScreenWidth - Theme::kPadding * 2,
               Theme::kScreenHeight - Theme::kFooterHeight - 30,
               CRenderer::FS_SMALL, Theme::kTextDim, CRenderer::ALIGN_RIGHT);
}
