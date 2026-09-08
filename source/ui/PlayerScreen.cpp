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

    // 左栏布局。逻辑分辨率固定 1280x720，所以直接写成常量，
    // 绘制和触摸命中判定共用同一份，不会画一套、点另一套。
    const int kLeftX = 48;
    const int kLeftWidth = 260;

    const int kCoverSize = 200;
    const Rect kCoverRect{ kLeftX + (kLeftWidth - kCoverSize) / 2, 88, kCoverSize, kCoverSize };

    const int kInfoY = 298;                 // 标题基线
    const int kCounterY = 398;              // “第 N 首 / 共 M 首”

    // 走带按钮排成十字，位置与方向键一一对应：
    //        [上] 播放/暂停
    //  [左]        [右]      上一曲 / 下一曲
    //        [下] 停止
    // 屏幕上的布局本身就是键位说明，所以底栏不再重复方向键的指引。
    const int kBtnW = 56;
    const int kBtnH = 44;
    const int kBtnGap = 6;
    const int kCrossX = kLeftX + (kLeftWidth - (kBtnW * 3 + kBtnGap * 2)) / 2;
    const int kCrossY = 424;
    const int kColMid = kCrossX + kBtnW + kBtnGap;
    const int kRowMid = kCrossY + kBtnH + kBtnGap;
    const int kRowBottom = kRowMid + kBtnH + kBtnGap;

    const Rect kBtnPlay{ kColMid, kCrossY, kBtnW, kBtnH };
    const Rect kBtnPrev{ kCrossX, kRowMid, kBtnW, kBtnH };
    const Rect kBtnNext{ kColMid + kBtnW + kBtnGap, kRowMid, kBtnW, kBtnH };
    const Rect kBtnStop{ kColMid, kRowBottom, kBtnW, kBtnH };
    // 十字中心那格不放功能，只画个装饰性的轴心让它读起来像方向键
    const Rect kCrossHub{ kColMid, kRowMid, kBtnW, kBtnH };

    const int kProgressY = 586;
    const int kProgressH = 6;
    // 触摸热区比 6 像素的可视进度条大得多——手指点不了那么准
    const Rect kProgressHit{ kLeftX - 12, kProgressY - 20, kLeftWidth + 24, 44 };

    double Clamp01(double value)
    {
        return std::max(0.0, std::min(1.0, value));
    }
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
    // 走带控制不写在这里：屏幕上的十字按钮与方向键一一对应，本身就是说明
    return "ZL/ZR 快退/快进   摇杆↑↓ 音量   X 视图   Y 模式   "
           "B+Y 下载   - 列表   + 浏览   B++ 设置";
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
    if (input.IsDown(CInputMap::BTN_PLUS) && !input.IsHeld(CInputMap::BTN_B))
        ctx.next_screen = SCREEN_BROWSER;

    // 方向键：经典的走带控制。
    // 按住 B 时方向键左右是调歌词偏移，那种情况下要让开（见下面的 B 组合键分支）。
    if (!input.IsHeld(CInputMap::BTN_B))
    {
        if (input.IsDown(CInputMap::BTN_DPAD_LEFT))
            player.PlayPrevious();
        if (input.IsDown(CInputMap::BTN_DPAD_RIGHT))
            player.PlayNext(true);
        if (input.IsDown(CInputMap::BTN_DPAD_UP))
            player.PlayOrPause();
        if (input.IsDown(CInputMap::BTN_DPAD_DOWN))
        {
            player.Stop();
            ctx.ShowToast("已停止");
        }
    }

    // 音量挪到左摇杆上下：方向键已经让给走带控制了
    if (input.IsRepeat(CInputMap::BTN_STICK_UP))
    {
        player.AdjustVolume(kVolumeStep);
        m_volume_overlay_timer = 1.5;
    }
    if (input.IsRepeat(CInputMap::BTN_STICK_DOWN))
    {
        player.AdjustVolume(-kVolumeStep);
        m_volume_overlay_timer = 1.5;
    }
    if (m_volume_overlay_timer > 0.0)
        m_volume_overlay_timer -= delta_seconds;

    HandleTouch(ctx);

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
        if (input.IsDown(CInputMap::BTN_PLUS))
            ctx.next_screen = SCREEN_SETTINGS;
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

void CPlayerScreen::HandleTouch(ScreenContext& ctx)
{
    const CInputMap::TouchState& touch = ctx.input->GetTouch();
    CPlayer& player = *ctx.player;

    // ---- 进度条拖动 ----
    if (touch.pressed && kProgressHit.Contains(touch.x, touch.y) && player.GetLength() > 0)
        m_touch_seeking = true;

    if (m_touch_seeking)
    {
        double ratio = Clamp01(static_cast<double>(touch.x - kLeftX) / kLeftWidth);
        m_touch_seek_ms = static_cast<int>(player.GetLength() * ratio);
        if (!touch.touching)
        {
            // 松手才真正定位：拖动过程中反复 seek 会让解码器不停地重新缓冲
            player.SeekTo(m_touch_seek_ms);
            m_touch_seeking = false;
        }
        m_pressed_button = HIT_NONE;
        return;
    }

    // ---- 按钮 ----
    m_pressed_button = HIT_NONE;
    if (touch.touching)
    {
        if (kBtnPrev.Contains(touch.x, touch.y))      m_pressed_button = HIT_PREV;
        else if (kBtnPlay.Contains(touch.x, touch.y)) m_pressed_button = HIT_PLAY;
        else if (kBtnNext.Contains(touch.x, touch.y)) m_pressed_button = HIT_NEXT;
        else if (kBtnStop.Contains(touch.x, touch.y)) m_pressed_button = HIT_STOP;
    }

    // 划动不算点击，否则在屏幕上滑一下会误触发
    if (!touch.released || touch.IsDrag())
        return;

    if (kBtnPrev.Contains(touch.x, touch.y))
    {
        player.PlayPrevious();
    }
    else if (kBtnPlay.Contains(touch.x, touch.y))
    {
        player.PlayOrPause();
    }
    else if (kBtnNext.Contains(touch.x, touch.y))
    {
        player.PlayNext(true);
    }
    else if (kBtnStop.Contains(touch.x, touch.y))
    {
        player.Stop();
        ctx.ShowToast("已停止");
    }
    else if (kCoverRect.Contains(touch.x, touch.y))
    {
        m_view = static_cast<ViewMode>((m_view + 1) % VIEW_COUNT);
    }
}

int CPlayerScreen::GetDisplayPosition(ScreenContext& ctx) const
{
    CPlayer& player = *ctx.player;
    int length = player.GetLength();

    if (m_touch_seeking)
        return m_touch_seek_ms;

    // 右摇杆拖动时预览拖动后的位置
    int preview = player.GetPosition() + static_cast<int>(m_seek_accumulator);
    if (length > 0)
        preview = std::min(preview, length);
    return std::max(0, preview);
}

void CPlayerScreen::DrawCover(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    const int x = kCoverRect.x, y = kCoverRect.y, size = kCoverRect.w;

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

void CPlayerScreen::DrawSongInfo(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    CPlayer& player = *ctx.player;
    const SongInfo& song = player.GetCurrentSong();

    if (song.file_path.empty())
    {
        r.DrawText("没有正在播放的曲目", kLeftX, kInfoY, CRenderer::FS_LARGE, Theme::kTextDim);
        r.DrawText("按 + 浏览 SD 卡上的音乐", kLeftX, kInfoY + 44, CRenderer::FS_NORMAL,
                   Theme::kTextDisabled);
        return;
    }

    r.DrawTextEllipsis(song.GetTitle(), kLeftX, kInfoY, kLeftWidth, CRenderer::FS_HUGE,
                       Theme::kText);
    r.DrawTextEllipsis(song.GetArtist(), kLeftX, kInfoY + 46, kLeftWidth, CRenderer::FS_NORMAL,
                       Theme::kTextDim);
    if (!song.album.empty())
    {
        r.DrawTextEllipsis(song.album, kLeftX, kInfoY + 76, kLeftWidth, CRenderer::FS_SMALL,
                           Theme::kTextDisabled);
    }

    // 曲目计数：第几首 / 共几首
    int total = player.GetPlaylistSize();
    if (total > 0)
    {
        char buff[64];
        std::snprintf(buff, sizeof(buff), "第 %d 首 / 共 %d 首",
                      player.GetCurrentIndex() + 1, total);
        r.DrawText(buff, kLeftX, kCounterY, CRenderer::FS_SMALL, Theme::kTextDim);

        char fraction[32];
        std::snprintf(fraction, sizeof(fraction), "%d/%d", player.GetCurrentIndex() + 1, total);
        r.DrawText(fraction, kLeftX + kLeftWidth, kCounterY, CRenderer::FS_SMALL,
                   Theme::kAccent, CRenderer::ALIGN_RIGHT);
    }
}

void CPlayerScreen::DrawTransportButtons(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    const bool playing = ctx.player->IsPlaying();

    // 先画中心轴心，让四个按钮读起来是一个方向键而不是四个孤立的方块
    r.FillRoundRect(kCrossHub.x + 14, kCrossHub.y + 12, kCrossHub.w - 28, kCrossHub.h - 24, 4,
                    Theme::kPanel.WithAlpha(120));

    struct ButtonDef { const Rect& rect; HitButton id; };
    const ButtonDef buttons[] = {
        { kBtnPlay, HIT_PLAY },
        { kBtnPrev, HIT_PREV },
        { kBtnNext, HIT_NEXT },
        { kBtnStop, HIT_STOP },
    };

    for (const ButtonDef& button : buttons)
    {
        bool pressed = (m_pressed_button == button.id);
        r.FillRoundRect(button.rect.x, button.rect.y, button.rect.w, button.rect.h, 8,
                        pressed ? Theme::kAccentDim : Theme::kPanel);

        const int cx = button.rect.x + button.rect.w / 2;
        const int cy = button.rect.y + button.rect.h / 2;
        const Color icon = Theme::kText;

        switch (button.id)
        {
        case HIT_PLAY:
            if (playing)
            {
                // 暂停：两根竖条
                r.FillRect(cx - 8, cy - 9, 5, 18, icon);
                r.FillRect(cx + 3, cy - 9, 5, 18, icon);
            }
            else
            {
                r.FillTriangle(cx - 6, cy - 10, cx - 6, cy + 10, cx + 9, cy, icon);
            }
            break;
        case HIT_PREV:
            r.FillRect(cx - 11, cy - 9, 3, 18, icon);
            r.FillTriangle(cx + 10, cy - 10, cx + 10, cy + 10, cx - 5, cy, icon);
            break;
        case HIT_NEXT:
            r.FillTriangle(cx - 10, cy - 10, cx - 10, cy + 10, cx + 5, cy, icon);
            r.FillRect(cx + 8, cy - 9, 3, 18, icon);
            break;
        case HIT_STOP:
            r.FillRect(cx - 8, cy - 8, 16, 16, icon);
            break;
        default:
            break;
        }
    }
}

void CPlayerScreen::DrawProgressBar(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    CPlayer& player = *ctx.player;

    const int length = player.GetLength();
    const int preview = GetDisplayPosition(ctx);
    const bool dragging = m_seeking || m_touch_seeking;

    r.FillRoundRect(kLeftX, kProgressY, kLeftWidth, kProgressH, kProgressH / 2, Theme::kPanelAlt);

    if (length > 0)
    {
        double ratio = Clamp01(static_cast<double>(preview) / length);
        int filled = static_cast<int>(kLeftWidth * ratio);
        r.FillRoundRect(kLeftX, kProgressY, filled, kProgressH, kProgressH / 2,
                        dragging ? Theme::kHighlight : Theme::kAccent);
        // 拖动手柄
        r.FillRoundRect(kLeftX + filled - 6, kProgressY - 5, 12, 16, 6, Theme::kText);
    }

    CPlayTime pos_time{ preview };
    CPlayTime len_time{ length };
    r.DrawText(pos_time.toString(false), kLeftX, kProgressY + 16, CRenderer::FS_SMALL,
               Theme::kTextDim);
    r.DrawText(length > 0 ? len_time.toString(false) : std::string("-:--"),
               kLeftX + kLeftWidth, kProgressY + 16, CRenderer::FS_SMALL, Theme::kTextDim,
               CRenderer::ALIGN_RIGHT);
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

    // 左侧：封面 + 曲目信息 + 走带按钮 + 进度条；右侧：歌词或频谱
    const int right_x = kLeftX + kLeftWidth + Theme::kPadding * 2;
    const int right_width = Theme::kScreenWidth - right_x - Theme::kPadding * 2;

    DrawCover(ctx);
    DrawSongInfo(ctx);
    DrawTransportButtons(ctx);
    DrawProgressBar(ctx);

    if (m_view == VIEW_LYRIC)
        DrawLyricView(ctx, right_x, content_top + 16, right_width, content_height - 32);
    else
        DrawSpectrumView(ctx, right_x, content_top + 16, right_width, content_height - 32);

    DrawVolumeOverlay(ctx);
}
