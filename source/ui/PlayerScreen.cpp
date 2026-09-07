#include "PlayerScreen.h"
#include "Renderer.h"
#include "../Player.h"
#include "../input/InputMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    const int kSeekStep = 5000;             // ZL/ZR 快进快退步长（毫秒）
    const int kVolumeStep = 5;
}

void CPlayerScreen::ReleaseResources(ScreenContext& ctx)
{
    if (m_cover != nullptr)
    {
        ctx.renderer->FreeTexture(m_cover);
        m_cover = nullptr;
    }
    m_cover_source.clear();
}

void CPlayerScreen::InvalidateCover(ScreenContext& ctx)
{
    if (m_cover != nullptr)
    {
        ctx.renderer->FreeTexture(m_cover);
        m_cover = nullptr;
    }
    // 清掉来源路径，RefreshCover 下次就会认为需要重新加载
    m_cover_source.clear();
}

void CPlayerScreen::OnEnter(ScreenContext& ctx)
{
    RefreshCover(ctx);
    m_last_lyric_index = -1;
    m_lyric_scroll = 0.0;
}

const char* CPlayerScreen::GetButtonHints() const
{
    return "A 播放/暂停   L/R 上下曲   ZL/ZR 快退/快进   X 切换视图   Y 播放模式   "
           "B+Y 在线下载   - 播放列表";
}

void CPlayerScreen::RefreshCover(ScreenContext& ctx)
{
    const std::string& path = ctx.player->GetCurrentSong().file_path;
    if (path == m_cover_source)
        return;

    if (m_cover != nullptr)
    {
        ctx.renderer->FreeTexture(m_cover);
        m_cover = nullptr;
    }
    m_cover_source = path;
    if (!path.empty())
        m_cover = ctx.renderer->LoadCoverImage(path);
}

void CPlayerScreen::Update(ScreenContext& ctx, double delta_seconds)
{
    CPlayer& player = *ctx.player;
    CInputMap& input = *ctx.input;

    RefreshCover(ctx);

    if (input.IsDown(CInputMap::BTN_A))
        player.PlayOrPause();

    if (input.IsDown(CInputMap::BTN_R))
        player.PlayNext(true);
    if (input.IsDown(CInputMap::BTN_L))
        player.PlayPrevious();

    if (input.IsRepeat(CInputMap::BTN_ZR))
        player.SeekRelative(kSeekStep);
    if (input.IsRepeat(CInputMap::BTN_ZL))
        player.SeekRelative(-kSeekStep);

    if (input.IsDown(CInputMap::BTN_X))
        m_view = static_cast<ViewMode>((m_view + 1) % VIEW_COUNT);

    // 按住 B 时 Y 是"打开下载界面"，这里要让开
    if (input.IsDown(CInputMap::BTN_Y) && !input.IsHeld(CInputMap::BTN_B))
    {
        player.SwitchRepeatMode();
        ctx.ShowToast(CPlayer::GetRepeatModeName(player.GetRepeatMode()));
    }

    if (input.IsDown(CInputMap::BTN_MINUS))
        ctx.next_screen = SCREEN_PLAYLIST;
    if (input.IsDown(CInputMap::BTN_PLUS))
        ctx.next_screen = SCREEN_BROWSER;

    // 方向键上下调音量
    if (input.IsRepeat(CInputMap::BTN_UP))
    {
        player.AdjustVolume(kVolumeStep);
        m_volume_overlay_timer = 1.5;
    }
    if (input.IsRepeat(CInputMap::BTN_DOWN))
    {
        player.AdjustVolume(-kVolumeStep);
        m_volume_overlay_timer = 1.5;
    }
    if (m_volume_overlay_timer > 0.0)
        m_volume_overlay_timer -= delta_seconds;

    // 右摇杆左右拖动进度：按住时按比例累计，松开时一次性定位，
    // 避免每帧都调用一次 Mix_SetMusicPosition
    float stick_x = input.GetRightStickX();
    if (std::fabs(stick_x) > 0.01f)
    {
        m_seeking = true;
        m_seek_accumulator += stick_x * delta_seconds * 30000.0;   // 满推约 30 秒/秒
    }
    else if (m_seeking)
    {
        player.SeekRelative(static_cast<int>(m_seek_accumulator));
        m_seek_accumulator = 0.0;
        m_seeking = false;
    }

    // B 键在播放界面用于调整歌词偏移（配合方向键左右），
    // 以及打开在线下载界面（B + Y）
    if (input.IsHeld(CInputMap::BTN_B))
    {
        if (input.IsRepeat(CInputMap::BTN_RIGHT))
        {
            player.AdjustLyricOffset(500);
            ctx.ShowToast("歌词延后 0.5 秒");
        }
        if (input.IsRepeat(CInputMap::BTN_LEFT))
        {
            player.AdjustLyricOffset(-500);
            ctx.ShowToast("歌词提前 0.5 秒");
        }
        if (input.IsDown(CInputMap::BTN_Y))
        {
            if (player.GetCurrentSong().file_path.empty())
                ctx.ShowToast("没有正在播放的曲目");
            else
                ctx.next_screen = SCREEN_DOWNLOAD;
        }
    }

    // 歌词滚动的平滑过渡
    int current_index = player.GetLyrics().GetLyricIndex(player.GetPosition());
    if (current_index != m_last_lyric_index)
    {
        m_last_lyric_index = current_index;
        m_lyric_scroll = 1.0;               // 从 1 衰减到 0，作为切换动画的进度
    }
    if (m_lyric_scroll > 0.0)
        m_lyric_scroll = std::max(0.0, m_lyric_scroll - delta_seconds * 5.0);
}

void CPlayerScreen::DrawCover(ScreenContext& ctx, int x, int y, int size)
{
    CRenderer& r = *ctx.renderer;

    if (m_cover != nullptr)
    {
        r.DrawTexture(m_cover, x, y, size, size);
    }
    else
    {
        // 没有封面时画一个占位的音符方块
        r.FillRoundRect(x, y, size, size, 12, Theme::kPanel);
        r.DrawText("♪", x + size / 2, y + size / 2 - 40, CRenderer::FS_HUGE,
                   Theme::kTextDisabled, CRenderer::ALIGN_CENTER);
    }
    r.DrawRect(x, y, size, size, Theme::kSeparator);
}

void CPlayerScreen::DrawSongInfo(ScreenContext& ctx, int x, int y, int width)
{
    CRenderer& r = *ctx.renderer;
    const SongInfo& song = ctx.player->GetCurrentSong();

    if (song.file_path.empty())
    {
        r.DrawText("没有正在播放的曲目", x, y, CRenderer::FS_LARGE, Theme::kTextDim);
        r.DrawText("按 + 浏览 SD 卡上的音乐", x, y + 44, CRenderer::FS_NORMAL, Theme::kTextDisabled);
        return;
    }

    r.DrawTextEllipsis(song.GetTitle(), x, y, width, CRenderer::FS_HUGE, Theme::kText);
    r.DrawTextEllipsis(song.GetArtist(), x, y + 52, width, CRenderer::FS_NORMAL, Theme::kTextDim);
    if (!song.album.empty())
        r.DrawTextEllipsis(song.album, x, y + 86, width, CRenderer::FS_SMALL, Theme::kTextDisabled);
}

void CPlayerScreen::DrawProgressBar(ScreenContext& ctx, int x, int y, int width)
{
    CRenderer& r = *ctx.renderer;
    CPlayer& player = *ctx.player;

    int position = player.GetPosition();
    int length = player.GetLength();

    // 正在用右摇杆拖动时，进度条要预览拖动后的位置
    int preview = position + static_cast<int>(m_seek_accumulator);
    preview = std::max(0, length > 0 ? std::min(preview, length) : preview);

    const int bar_height = 6;
    r.FillRoundRect(x, y, width, bar_height, bar_height / 2, Theme::kPanelAlt);

    if (length > 0)
    {
        double ratio = static_cast<double>(preview) / length;
        ratio = std::max(0.0, std::min(1.0, ratio));
        int filled = static_cast<int>(width * ratio);
        r.FillRoundRect(x, y, filled, bar_height, bar_height / 2,
                        m_seeking ? Theme::kHighlight : Theme::kAccent);
        // 拖动手柄
        r.FillRoundRect(x + filled - 6, y - 5, 12, 16, 6, Theme::kText);
    }

    CPlayTime pos_time{ preview };
    CPlayTime len_time{ length };
    r.DrawText(pos_time.toString(false), x, y + 16, CRenderer::FS_SMALL, Theme::kTextDim);
    r.DrawText(length > 0 ? len_time.toString(false) : std::string("-:--"),
               x + width, y + 16, CRenderer::FS_SMALL, Theme::kTextDim, CRenderer::ALIGN_RIGHT);
}

void CPlayerScreen::DrawLyricView(ScreenContext& ctx, int x, int y, int width, int height)
{
    CRenderer& r = *ctx.renderer;
    CPlayer& player = *ctx.player;
    const CLrcParser& lyrics = player.GetLyrics();

    if (lyrics.IsEmpty())
    {
        r.DrawText("暂无歌词", x + width / 2, y + height / 2 - 20, CRenderer::FS_LARGE,
                   Theme::kTextDisabled, CRenderer::ALIGN_CENTER);
        r.DrawText("把同名 .lrc 文件放在歌曲旁边即可", x + width / 2, y + height / 2 + 20,
                   CRenderer::FS_SMALL, Theme::kTextDisabled, CRenderer::ALIGN_CENTER);
        return;
    }

    const std::vector<CLrcParser::Lyric>& lines = lyrics.GetLyrics();
    int position = player.GetPosition();
    int current = lyrics.GetLyricIndex(position);

    const int line_height = 52;
    const int center_y = y + height / 2 - line_height / 2;
    const int visible = height / line_height / 2 + 1;

    r.PushClip(x, y, width, height);

    // 当前行居中，上下各画若干行；切行时用 m_lyric_scroll 做一点位移动画
    double animation_offset = m_lyric_scroll * line_height;
    for (int offset = -visible; offset <= visible; ++offset)
    {
        int index = current + offset;
        if (index < 0 || index >= static_cast<int>(lines.size()))
            continue;

        const CLrcParser::Lyric& line = lines[index];
        int draw_y = center_y + offset * line_height + static_cast<int>(animation_offset);
        bool is_current = (offset == 0);

        if (is_current && !line.split.empty())
        {
            // 逐字歌词：先画底色整行，再用当前进度裁出已唱部分
            int text_w = 0, text_h = 0;
            r.MeasureText(line.text, CRenderer::FS_LYRIC_CURRENT, text_w, text_h);
            int text_x = x + width / 2 - text_w / 2;

            r.DrawText(line.text, text_x, draw_y, CRenderer::FS_LYRIC_CURRENT, Theme::kLyricOther);

            // 找出当前落在哪一段，算出该段内的比例
            int elapsed = position - line.time_start;
            size_t sung_bytes = 0;
            int accumulated = 0;
            double partial = 0.0;
            for (size_t seg = 0; seg < line.word_time.size(); ++seg)
            {
                int seg_time = line.word_time[seg];
                if (elapsed >= accumulated + seg_time)
                {
                    accumulated += seg_time;
                    sung_bytes = line.split[seg];
                    continue;
                }
                if (seg_time > 0)
                    partial = static_cast<double>(elapsed - accumulated) / seg_time;
                size_t seg_start = (seg == 0) ? 0 : line.split[seg - 1];
                size_t seg_end = line.split[seg];
                // 段内按比例插值出一个字节位置，再回退到 UTF-8 字符边界
                size_t target = seg_start + static_cast<size_t>((seg_end - seg_start) * partial);
                while (target > seg_start && target < line.text.size()
                       && (static_cast<unsigned char>(line.text[target]) & 0xC0) == 0x80)
                {
                    --target;
                }
                sung_bytes = target;
                break;
            }

            if (sung_bytes > 0)
            {
                std::string sung = line.text.substr(0, sung_bytes);
                r.DrawText(sung, text_x, draw_y, CRenderer::FS_LYRIC_CURRENT, Theme::kLyricKaraoke);
            }
        }
        else
        {
            Color color = is_current ? Theme::kLyricCurrent : Theme::kLyricOther;
            CRenderer::FontSize font = is_current ? CRenderer::FS_LYRIC_CURRENT : CRenderer::FS_LYRIC;
            r.DrawTextEllipsis(line.text, x + width / 2, draw_y, width, font, color,
                               CRenderer::ALIGN_CENTER);
        }

        // 翻译画在本行下方
        if (!line.translate.empty() && player.GetConfig().GetShowTranslation())
        {
            r.DrawTextEllipsis(line.translate, x + width / 2, draw_y + 40, width,
                               CRenderer::FS_SMALL,
                               is_current ? Theme::kLyricTranslate : Theme::kLyricOther,
                               CRenderer::ALIGN_CENTER);
        }
    }

    r.PopClip();

    if (player.GetLyricOffset() != 0)
    {
        char buff[64];
        std::snprintf(buff, sizeof(buff), "歌词偏移 %+.1f 秒", player.GetLyricOffset() / 1000.0);
        r.DrawText(buff, x + width - 8, y + height - 28, CRenderer::FS_SMALL,
                   Theme::kTextDisabled, CRenderer::ALIGN_RIGHT);
    }
}

void CPlayerScreen::DrawSpectrumView(ScreenContext& ctx, int x, int y, int width, int height)
{
    CRenderer& r = *ctx.renderer;
    const std::array<float, CSpectrumAnalyzer::kBarCount>& bars =
        ctx.player->GetAudio().GetSpectrum().GetBars();

    const int count = CSpectrumAnalyzer::kBarCount;
    const int gap = 4;
    const int bar_width = std::max(1, (width - gap * (count - 1)) / count);
    const int base_y = y + height - 20;
    const int max_height = height - 40;

    for (int i = 0; i < count; ++i)
    {
        int bar_height = static_cast<int>(bars[i] * max_height);
        bar_height = std::max(2, bar_height);
        int bar_x = x + i * (bar_width + gap);

        // 低频到高频做一个颜色渐变
        float t = static_cast<float>(i) / (count - 1);
        Color color{
            static_cast<uint8_t>(Theme::kSpectrumLow.r + (Theme::kSpectrumHigh.r - Theme::kSpectrumLow.r) * t),
            static_cast<uint8_t>(Theme::kSpectrumLow.g + (Theme::kSpectrumHigh.g - Theme::kSpectrumLow.g) * t),
            static_cast<uint8_t>(Theme::kSpectrumLow.b + (Theme::kSpectrumHigh.b - Theme::kSpectrumLow.b) * t)
        };
        r.FillRoundRect(bar_x, base_y - bar_height, bar_width, bar_height, 3, color);
    }
}

void CPlayerScreen::DrawVolumeOverlay(ScreenContext& ctx)
{
    if (m_volume_overlay_timer <= 0.0)
        return;

    CRenderer& r = *ctx.renderer;
    int volume = ctx.player->GetVolume();

    const int w = 280, h = 72;
    const int x = Theme::kScreenWidth - w - Theme::kPadding;
    const int y = Theme::kHeaderHeight + Theme::kPadding;

    // 快消失时淡出
    uint8_t alpha = static_cast<uint8_t>(std::min(1.0, m_volume_overlay_timer / 0.4) * 235);
    r.FillRoundRect(x, y, w, h, 10, Theme::kPanel.WithAlpha(alpha));

    char buff[32];
    std::snprintf(buff, sizeof(buff), "音量 %d%%", volume);
    r.DrawText(buff, x + 16, y + 10, CRenderer::FS_SMALL, Theme::kText.WithAlpha(alpha));

    const int bar_x = x + 16, bar_y = y + 44, bar_w = w - 32;
    r.FillRoundRect(bar_x, bar_y, bar_w, 8, 4, Theme::kPanelAlt.WithAlpha(alpha));
    r.FillRoundRect(bar_x, bar_y, bar_w * volume / 100, 8, 4, Theme::kAccent.WithAlpha(alpha));
}

void CPlayerScreen::Draw(ScreenContext& ctx)
{
    const int content_top = Theme::kHeaderHeight;
    const int content_height = Theme::kScreenHeight - Theme::kHeaderHeight - Theme::kFooterHeight;

    // 左侧：封面 + 曲目信息 + 进度条；右侧：歌词或频谱
    const int cover_size = 280;
    const int left_x = Theme::kPadding * 2;
    const int left_width = cover_size;
    const int right_x = left_x + left_width + Theme::kPadding * 2;
    const int right_width = Theme::kScreenWidth - right_x - Theme::kPadding * 2;

    DrawCover(ctx, left_x, content_top + 32, cover_size);
    DrawSongInfo(ctx, left_x, content_top + 32 + cover_size + 24, left_width);
    DrawProgressBar(ctx, left_x, content_top + content_height - 56, left_width);

    if (m_view == VIEW_LYRIC)
        DrawLyricView(ctx, right_x, content_top + 16, right_width, content_height - 32);
    else
        DrawSpectrumView(ctx, right_x, content_top + 16, right_width, content_height - 32);

    DrawVolumeOverlay(ctx);
}
