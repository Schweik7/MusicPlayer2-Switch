#include "BrowserScreen.h"
#include "ListScroller.h"
#include "Renderer.h"
#include "../Player.h"
#include "../core/MediaScanner.h"
#include "../core/PlaylistFile.h"
#include "../input/InputMap.h"

#include <algorithm>
#include <cstdio>

namespace
{
    int VisibleCount()
    {
        int content_height = Theme::kScreenHeight - Theme::kHeaderHeight - Theme::kFooterHeight
                             - Theme::kPadding * 2 - 40;     // 40 是当前路径那一行
        return content_height / Theme::kListItemHeight;
    }

    const char* kRootDir = "sdmc:/";

    // 列表首行的 y。40 是上方"当前路径"那一行占掉的高度，
    // 绘制、点击命中和拖动滚动必须用同一个值
    int ListTop()
    {
        return Theme::kHeaderHeight + Theme::kPadding + 40;
    }
}

void CBrowserScreen::OnEnter(ScreenContext& ctx)
{
    if (m_dir.empty())
    {
        // 首次进入落在配置的音乐目录，没有就退到 SD 卡根目录
        std::string music_dir = ctx.player->GetConfig().GetMusicDir();
        Navigate(ctx, FileUtil::IsDirectory(music_dir) ? music_dir : kRootDir);
    }
}

const char* CBrowserScreen::GetButtonHints() const
{
    return "A 打开/播放|B 上级目录|X 播放整个目录|Y 设为音乐目录|− 播放列表|＋ 设置";
}

void CBrowserScreen::Navigate(ScreenContext& ctx, const std::string& dir)
{
    (void)ctx;
    m_error.clear();
    m_items.clear();
    m_selected = 0;
    m_scroll = 0;
    m_scroll_smooth = 0.0;

    std::vector<FileUtil::DirEntry> entries;
    if (!FileUtil::ListDir(dir, entries))
    {
        m_error = "无法打开目录: " + dir;
        return;
    }
    m_dir = dir;

    for (const FileUtil::DirEntry& entry : entries)
    {
        Item item;
        item.name = entry.name;
        item.is_dir = entry.is_dir;
        if (!entry.is_dir)
        {
            item.is_audio = CMediaScanner::IsSupportedAudio(entry.name);
            item.is_playlist = CPlaylistFile::IsPlaylistFile(entry.name);
            // 只显示能用的文件，避免 SD 卡根目录被无关文件淹没
            if (!item.is_audio && !item.is_playlist)
                continue;
        }
        else if (entry.name[0] == '.')
        {
            continue;
        }
        m_items.push_back(item);
    }
}

void CBrowserScreen::GoUp(ScreenContext& ctx)
{
    std::string normalized = FileUtil::NormalizeSeparators(m_dir);
    // 已经在 "sdmc:/" 就不能再往上了
    if (normalized.size() <= 6)
        return;
    if (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();

    std::string parent = FileUtil::GetDir(normalized);
    if (parent.empty() || parent.find(":/") == std::string::npos)
        parent = kRootDir;

    std::string child_name = FileUtil::GetFileName(normalized);
    Navigate(ctx, parent);

    // 回到上级目录时把光标停在刚才那个子目录上
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].name == child_name)
        {
            m_selected = static_cast<int>(i);
            break;
        }
    }
    EnsureSelectionVisible(VisibleCount());
    m_scroll_smooth = m_scroll;
}

void CBrowserScreen::EnsureSelectionVisible(int visible_count)
{
    if (m_selected < m_scroll)
        m_scroll = m_selected;
    else if (m_selected >= m_scroll + visible_count)
        m_scroll = m_selected - visible_count + 1;
    m_scroll = std::max(0, m_scroll);
}

void CBrowserScreen::SetAsMusicDir(ScreenContext& ctx)
{
    CPlayer& player = *ctx.player;
    player.GetConfig().SetMusicDir(m_dir);
    player.GetPathMapper().SetDefaultMusicDir(m_dir);
    // 立刻落盘。配置原先只在退出时写，中途断电或崩溃这次设置就白做了；
    // 而且在没有 fsdevCommitDevice 之前，那次写入根本没到 SD 卡上——
    // 这正是"按了 Y 没反应"的直接原因。
    player.SaveConfig();

    // 光改配置对当前这次运行毫无可见效果（只影响下次启动、且要求没有上次的播放列表），
    // 用户自然会觉得这个键坏了。所以顺手把这个目录扫进播放列表。
    std::vector<SongInfo> songs;
    CMediaScanner::ScanDirectory(m_dir, songs, 3);
    if (songs.empty())
    {
        ctx.ShowToast("已设为默认音乐目录（目录内没有可播放的音频）");
        return;
    }

    // 不自动播放：用户按 Y 的意图是"以后从这里找歌"，不是"马上放"
    player.SetPlaylist(std::move(songs), 0, false);
    char message[128];
    std::snprintf(message, sizeof(message), "已设为默认音乐目录，载入 %d 首",
                  player.GetPlaylistSize());
    ctx.ShowToast(message);
}

void CBrowserScreen::PlayCurrentDirectory(ScreenContext& ctx, bool from_selection)
{
    std::vector<SongInfo> songs;
    // 只收当前目录这一层，不递归：SD 卡上的目录可能非常深，递归会让操作长时间无响应
    CMediaScanner::ScanDirectory(m_dir, songs, 0);
    if (songs.empty())
    {
        ctx.ShowToast("该目录下没有可播放的音频");
        return;
    }

    int play_index = 0;
    if (from_selection && m_selected >= 0 && m_selected < static_cast<int>(m_items.size()))
    {
        std::string target = FileUtil::Combine(m_dir, m_items[m_selected].name);
        for (size_t i = 0; i < songs.size(); ++i)
        {
            if (songs[i].file_path == target)
            {
                play_index = static_cast<int>(i);
                break;
            }
        }
    }

    ctx.player->SetPlaylist(std::move(songs), play_index, true);
    ctx.next_screen = SCREEN_PLAYER;
}

void CBrowserScreen::Activate(ScreenContext& ctx)
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size()))
        return;

    const Item& item = m_items[m_selected];
    std::string full_path = FileUtil::Combine(m_dir, item.name);

    if (item.is_dir)
    {
        Navigate(ctx, full_path);
        return;
    }
    if (item.is_playlist)
    {
        if (ctx.player->LoadPlaylistFile(full_path, true))
            ctx.next_screen = SCREEN_PLAYER;
        else
            ctx.ShowToast("无法读取该播放列表");
        return;
    }
    // 音频文件：把同目录的曲目一起放进播放列表，从选中的这首开始
    PlayCurrentDirectory(ctx, true);
}

void CBrowserScreen::Update(ScreenContext& ctx, double delta_seconds)
{
    CInputMap& input = *ctx.input;
    const int count = static_cast<int>(m_items.size());
    const int visible = VisibleCount();

    if (input.IsDown(CInputMap::BTN_MINUS))
    {
        ctx.next_screen = SCREEN_PLAYLIST;
        return;
    }
    if (input.IsDown(CInputMap::BTN_PLUS))
    {
        ctx.next_screen = SCREEN_SETTINGS;
        return;
    }
    if (input.IsDown(CInputMap::BTN_B))
    {
        // 在根目录按 B 直接回播放界面
        if (FileUtil::NormalizeSeparators(m_dir).size() <= 6)
            ctx.next_screen = SCREEN_PLAYER;
        else
            GoUp(ctx);
        return;
    }

    bool touch_scrolled = false;
    if (count > 0)
    {
        if (input.IsRepeat(CInputMap::BTN_DOWN))
            m_selected = (m_selected + 1) % count;
        if (input.IsRepeat(CInputMap::BTN_UP))
            m_selected = (m_selected - 1 + count) % count;
        if (input.IsRepeat(CInputMap::BTN_R))
            m_selected = std::min(count - 1, m_selected + visible);
        if (input.IsRepeat(CInputMap::BTN_L))
            m_selected = std::max(0, m_selected - visible);

        if (input.IsDown(CInputMap::BTN_A))
            Activate(ctx);
        if (input.IsDown(CInputMap::BTN_X))
            PlayCurrentDirectory(ctx, false);

        // 触摸拖动滚动（含松手惯性）
        ListScroller::Params params;
        params.list_top = ListTop();
        params.item_height = Theme::kListItemHeight;
        params.count = count;
        params.visible = visible;
        touch_scrolled = ListScroller::Update(input.GetTouch(), params, delta_seconds,
                                              m_scroll, m_scroll_smooth, m_dragging, m_fling);

        // 触摸选中/打开。划动过就不算点击，避免滑列表时误进目录
        const CInputMap::TouchState& touch = input.GetTouch();
        if (touch.released && !touch.IsDrag())
        {
            int list_top = ListTop();
            if (touch.y >= list_top && touch.y < list_top + visible * Theme::kListItemHeight)
            {
                int row = (touch.y - list_top) / Theme::kListItemHeight;
                int index = m_scroll + row;
                if (index >= 0 && index < count)
                {
                    m_selected = index;
                    Activate(ctx);
                }
            }
        }
    }

    if (input.IsDown(CInputMap::BTN_Y))
        SetAsMusicDir(ctx);

    if (touch_scrolled)
    {
        // 触摸在主导滚动：把光标拉进可见范围，而不是反过来把列表拽回光标处
        m_selected = std::max(m_scroll, std::min(m_selected, m_scroll + visible - 1));
        m_selected = std::max(0, std::min(m_selected, std::max(0, count - 1)));
        return;
    }

    EnsureSelectionVisible(visible);

    double diff = m_scroll - m_scroll_smooth;
    if (std::abs(diff) < 0.01)
        m_scroll_smooth = m_scroll;
    else
        m_scroll_smooth += diff * std::min(1.0, delta_seconds * 18.0);
}

void CBrowserScreen::Draw(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;

    const int list_x = Theme::kPadding * 2;
    const int path_y = Theme::kHeaderHeight + Theme::kPadding;
    const int list_y = ListTop();
    const int list_w = Theme::kScreenWidth - list_x * 2 - 12;
    const int visible = VisibleCount();

    // 当前路径
    r.DrawTextEllipsis(m_dir, list_x, path_y, list_w, CRenderer::FS_SMALL, Theme::kAccent);

    if (!m_error.empty())
    {
        r.DrawText(m_error, Theme::kScreenWidth / 2, list_y + 100, CRenderer::FS_NORMAL,
                   Theme::kTextDim, CRenderer::ALIGN_CENTER);
        return;
    }
    if (m_items.empty())
    {
        r.DrawText("该目录下没有音频文件或子目录", Theme::kScreenWidth / 2, list_y + 100,
                   CRenderer::FS_NORMAL, Theme::kTextDim, CRenderer::ALIGN_CENTER);
        return;
    }

    r.PushClip(list_x, list_y, list_w, visible * Theme::kListItemHeight);

    int first = static_cast<int>(m_scroll_smooth);
    int pixel_offset = static_cast<int>((m_scroll_smooth - first) * Theme::kListItemHeight);

    for (int row = 0; row <= visible; ++row)
    {
        int index = first + row;
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            continue;

        const Item& item = m_items[index];
        int item_y = list_y + row * Theme::kListItemHeight - pixel_offset;
        bool is_selected = (index == m_selected);

        if (is_selected)
            r.FillRoundRect(list_x, item_y + 2, list_w, Theme::kListItemHeight - 4, 6, Theme::kSelection);

        // 类型图标
        const char* icon = item.is_dir ? "▶" : (item.is_playlist ? "≡" : "♪");
        Color icon_color = item.is_dir ? Theme::kHighlight
                                       : (item.is_playlist ? Theme::kAccent : Theme::kTextDim);
        r.DrawText(icon, list_x + 16, item_y + 14, CRenderer::FS_NORMAL, icon_color);

        r.DrawTextEllipsis(item.name, list_x + 52, item_y + 14, list_w - 68,
                           CRenderer::FS_NORMAL, Theme::kText);
    }

    r.PopClip();

    // 滚动条
    const int track_x = list_x + list_w + 4;
    const int track_h = visible * Theme::kListItemHeight;
    r.FillRoundRect(track_x, list_y, 6, track_h, 3, Theme::kPanelAlt);
    if (static_cast<int>(m_items.size()) > visible)
    {
        int thumb_h = std::max(30, track_h * visible / static_cast<int>(m_items.size()));
        int max_scroll = static_cast<int>(m_items.size()) - visible;
        int thumb_y = list_y + static_cast<int>((track_h - thumb_h) * m_scroll_smooth / max_scroll);
        r.FillRoundRect(track_x, thumb_y, 6, thumb_h, 3, Theme::kAccent);
    }
}
