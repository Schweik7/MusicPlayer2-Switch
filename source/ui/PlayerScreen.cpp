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
    const int kFineSeekStep = 1000;         // 左摇杆左右：逐秒微调
    const int kVolumeStep = 5;

    // 左栏布局。逻辑分辨率固定 1280x720，所以直接写成常量，
    // 绘制和触摸命中判定共用同一份，不会画一套、点另一套。
    const int kLeftX = 48;
    const int kLeftWidth = 260;

    // 曲目计数挪到歌词区左下角之后，这里空出一行的高度，正好给封面。
    const int kCoverSize = 226;
    const Rect kCoverRect{ kLeftX + (kLeftWidth - kCoverSize) / 2, 88, kCoverSize, kCoverSize };

    const int kInfoY = 322;                 // 标题基线

    // 走带按钮排成十字，位置与方向键一一对应：
    //        [上] 播放/暂停
    //  [左]        [右]      上一曲 / 下一曲
    //        [下] 停止
    // 屏幕上的布局本身就是键位说明，所以底栏不再重复方向键的指引。
    // 做成正方形：方向键本来就是四个等大的键，扁矩形读起来不像。
    const int kBtnSize = 48;
    const int kBtnGap = 5;
    const int kCrossX = kLeftX + (kLeftWidth - (kBtnSize * 3 + kBtnGap * 2)) / 2;
    const int kCrossY = 422;
    const int kColMid = kCrossX + kBtnSize + kBtnGap;
    const int kRowMid = kCrossY + kBtnSize + kBtnGap;
    const int kRowBottom = kRowMid + kBtnSize + kBtnGap;

    const Rect kBtnPlay{ kColMid, kCrossY, kBtnSize, kBtnSize };
    const Rect kBtnPrev{ kCrossX, kRowMid, kBtnSize, kBtnSize };
    const Rect kBtnNext{ kColMid + kBtnSize + kBtnGap, kRowMid, kBtnSize, kBtnSize };
    const Rect kBtnStop{ kColMid, kRowBottom, kBtnSize, kBtnSize };
    // 十字中心那格不放功能，只画个装饰性的轴心让它读起来像方向键
    const Rect kCrossHub{ kColMid, kRowMid, kBtnSize, kBtnSize };

    const int kProgressY = 592;
    const int kProgressH = 6;
    // 触摸热区比 6 像素的可视进度条大得多——手指点不了那么准
    const Rect kProgressHit{ kLeftX - 12, kProgressY - 20, kLeftWidth + 24, 44 };

    // 右栏（歌词 / 频谱）。绘制与触摸判定共用。
    const int kRightX = kLeftX + kLeftWidth + Theme::kPadding * 2;
    const int kRightWidth = Theme::kScreenWidth - kRightX - Theme::kPadding * 2;
    const int kRightY = Theme::kHeaderHeight + 16;
    const int kRightHeight = Theme::kScreenHeight - Theme::kHeaderHeight - Theme::kFooterHeight - 32;

    // 歌词区右上角：下载、拖歌词是否带进度、单栏/双栏
    const int kToolSize = 40;
    const int kToolGap = 8;
    const Rect kBtnLayout{ kRightX + kRightWidth - kToolSize, kRightY, kToolSize, kToolSize };
    const Rect kBtnSync{ kBtnLayout.x - kToolSize - kToolGap, kRightY, kToolSize, kToolSize };
    const Rect kBtnDownload{ kBtnSync.x - kToolSize - kToolGap, kRightY, kToolSize, kToolSize };

    // 歌词区左上角：歌词偏移和音量各自一组"减 · 读数 · 加"。
    // 和左下角的方向键十字、右下角的 ABXY 菱形一样，是一个自带说明的操作区；
    // 音量原本只在顶栏显示一个百分数，既不直观也占着底栏的提示位置。
    const int kStepLabelW = 76;
    const Rect kBtnOffsetMinus{ kRightX, kRightY, kToolSize, kToolSize };
    const Rect kBtnOffsetPlus{ kRightX + kToolSize + kStepLabelW, kRightY, kToolSize, kToolSize };
    const int kVolumeGroupX = kBtnOffsetPlus.x + kToolSize + 20;
    const int kVolumeLabelW = 56;
    const Rect kBtnVolumeMinus{ kVolumeGroupX, kRightY, kToolSize, kToolSize };
    const Rect kBtnVolumePlus{ kVolumeGroupX + kToolSize + kVolumeLabelW, kRightY, kToolSize,
                               kToolSize };

    // 右下角的 ABXY，按手柄上的实际方位摆成菱形：
    //          [X] 视图
    //   [Y] 模式     [A] 播放
    //          [B] 偏移
    // 每格里上面是按键字母、下面是功能名，点一下等同按那个键。
    const int kFaceCellW = 62;
    const int kFaceCellH = 62;
    const int kFaceRight = kRightX + kRightWidth;
    const int kFaceBottom = kRightY + kRightHeight;
    const int kFaceLeft = kFaceRight - kFaceCellW * 3;
    const int kFaceTop = kFaceBottom - kFaceCellH * 3;

    const Rect kBtnFaceX{ kFaceLeft + kFaceCellW, kFaceTop, kFaceCellW, kFaceCellH };
    const Rect kBtnFaceY{ kFaceLeft, kFaceTop + kFaceCellH, kFaceCellW, kFaceCellH };
    const Rect kBtnFaceA{ kFaceLeft + kFaceCellW * 2, kFaceTop + kFaceCellH, kFaceCellW, kFaceCellH };
    const Rect kBtnFaceB{ kFaceLeft + kFaceCellW, kFaceTop + kFaceCellH * 2, kFaceCellW, kFaceCellH };

    // 松手后还按浏览位置停留多久，然后滑回当前播放的那句
    const double kLyricBrowseHold = 3.0;

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
    // 走带控制和 ABXY 都不写在这里：屏幕上的十字按钮与方向键一一对应，
    // 右下角的菱形按钮与 ABXY 一一对应，布局本身就是说明。
    // 用 '|' 分段，底栏会按段均匀铺开。
    return "ZL/ZR ±5秒|左摇杆←→ ±1秒|右摇杆↑↓ 翻歌词|按下右摇杆 下载|− 列表|＋ 设置";
}

void CPlayerScreen::GoBack(ScreenContext& ctx)
{
    (void)ctx;
    m_cover_fullscreen = false;
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

    // 封面全屏查看时只接受"退出"，避免误触其它功能
    if (m_cover_fullscreen)
    {
        const CInputMap::TouchState& touch = input.GetTouch();
        if (input.IsDown(CInputMap::BTN_B) || input.IsDown(CInputMap::BTN_A)
            || (touch.released && !touch.IsDrag()))
        {
            m_cover_fullscreen = false;
        }
        // 封面丢了（换歌）就自动退出，否则会停在一个空白页面上
        if (m_cover == nullptr)
            m_cover_fullscreen = false;
        return;
    }

    // 大封面视图下 A 键改成进全屏：这样"看大图"不再是触摸独有的操作。
    // 其它视图里 A 仍然是播放/暂停。
    if (input.IsDown(CInputMap::BTN_A))
    {
        if (m_view == VIEW_COVER && m_cover != nullptr)
            m_cover_fullscreen = true;
        else
            player.PlayOrPause();
    }

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

    if (input.IsDown(CInputMap::BTN_Y))
    {
        player.SwitchRepeatMode();
        ctx.ShowToast(CPlayer::GetRepeatModeName(player.GetRepeatMode()));
    }

    if (input.IsDown(CInputMap::BTN_MINUS))
        ctx.next_screen = SCREEN_PLAYLIST;
    if (input.IsDown(CInputMap::BTN_PLUS))
        ctx.next_screen = SCREEN_SETTINGS;

    // 按下右摇杆：在线下载歌词封面。
    // 原来是 B+Y 组合键，组合键既难记也难按，改成一个独立的键
    if (input.IsDown(CInputMap::BTN_STICK_R))
    {
        if (player.GetCurrentSong().file_path.empty())
            ctx.ShowToast("没有正在播放的曲目");
        else
            ctx.next_screen = SCREEN_DOWNLOAD;
    }

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

    // 左摇杆左右：逐秒快退/快进。ZL/ZR 是 5 秒一档，这里给一个更精细的档位，
    // 对歌词对轴之类的场合有用
    if (input.IsRepeat(CInputMap::BTN_STICK_RIGHT))
        player.SeekRelative(kFineSeekStep);
    if (input.IsRepeat(CInputMap::BTN_STICK_LEFT))
        player.SeekRelative(-kFineSeekStep);

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

    // 歌词区的拖动优先：手指在歌词上划的时候不该同时被当成点按钮
    if (!HandleLyricDrag(ctx, delta_seconds))
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

    // 按住 B + 方向键左右：调整歌词偏移。
    // 这里必须用只认方向键的 BTN_DPAD_*，因为摇杆左右已经分给逐秒快进退了
    if (input.IsHeld(CInputMap::BTN_B))
    {
        if (input.IsRepeat(CInputMap::BTN_DPAD_RIGHT))
        {
            player.AdjustLyricOffset(500);
            ctx.ShowToast("歌词延后 0.5 秒");
        }
        if (input.IsRepeat(CInputMap::BTN_DPAD_LEFT))
        {
            player.AdjustLyricOffset(-500);
            ctx.ShowToast("歌词提前 0.5 秒");
        }
    }

    // 右摇杆上下翻看歌词，和手指拖动是同一件事的按键版本。
    // 右摇杆左右已经分给拖进度了，上下正好空着。
    if (m_view == VIEW_LYRIC && !player.GetLyrics().IsEmpty())
    {
        const float stick_y = input.GetRightStickY();
        if (std::fabs(stick_y) > 0.01f)
        {
            m_lyric_browse_offset += stick_y * delta_seconds * 600.0;
            m_lyric_browse_hold = kLyricBrowseHold;
            m_lyric_stick_browsing = true;
        }
        else if (m_lyric_stick_browsing)
        {
            // 摇杆回中等同于松手，走同一条收尾逻辑
            m_lyric_stick_browsing = false;
            FinishLyricBrowse(ctx, true);
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

bool CPlayerScreen::IsTwoColumnLyric(ScreenContext& ctx) const
{
    const CConfig& config = ctx.player->GetConfig();
    if (!config.GetLyricTwoColumn() || !config.GetShowTranslation())
        return false;
    // 这首歌没有译文时右栏是一片空白，那就不算双栏
    for (const CLrcParser::Lyric& line : ctx.player->GetLyrics().GetLyrics())
    {
        if (!line.translate.empty())
            return true;
    }
    return false;
}

bool CPlayerScreen::FaceButtonsVisible(ScreenContext& ctx) const
{
    // 双栏歌词的右半边正好压在这组按钮上，那种排版下就不画了
    return !(m_view == VIEW_LYRIC && IsTwoColumnLyric(ctx));
}

void CPlayerScreen::ActivateFaceButton(ScreenContext& ctx, HitButton button)
{
    CPlayer& player = *ctx.player;
    switch (button)
    {
    case HIT_FACE_A:
        player.PlayOrPause();
        break;
    case HIT_FACE_X:
        m_view = static_cast<ViewMode>((m_view + 1) % VIEW_COUNT);
        break;
    case HIT_FACE_Y:
        player.SwitchRepeatMode();
        ctx.ShowToast(CPlayer::GetRepeatModeName(player.GetRepeatMode()));
        break;
    case HIT_FACE_B:
        // B 在这个界面上只作修饰键用，单点它没有对应动作，
        // 那就把它是干什么用的说出来，免得点了没反应像是坏了
        ctx.ShowToast("按住 B + 方向键 ← → 调整歌词偏移");
        break;
    default:
        break;
    }
}

void CPlayerScreen::FinishLyricBrowse(ScreenContext& ctx, bool moved)
{
    CPlayer& player = *ctx.player;
    // 定位放在"翻完了"这一刻做。翻的过程中每帧 seek 会让解码器不停重新缓冲，
    // 声音会碎掉；停下来再跳一次，听感上和实时跟随没有区别。
    if (!moved || !player.GetConfig().GetLyricSeekSync() || m_lyric_browse_index < 0)
        return;

    const std::vector<CLrcParser::Lyric>& lines = player.GetLyrics().GetLyrics();
    if (m_lyric_browse_index >= static_cast<int>(lines.size()))
        return;

    player.SeekTo(lines[m_lyric_browse_index].time_start);
    // 进度已经跟过去了，浏览偏移就该归零
    m_lyric_browse_offset = 0.0;
    m_lyric_browse_hold = 0.0;
}

bool CPlayerScreen::HandleLyricDrag(ScreenContext& ctx, double delta_seconds)
{
    const CInputMap::TouchState& touch = ctx.input->GetTouch();
    CPlayer& player = *ctx.player;

    if (m_view != VIEW_LYRIC || player.GetLyrics().IsEmpty())
    {
        m_lyric_dragging = false;
        m_lyric_browse_offset = 0.0;
        m_lyric_browse_hold = 0.0;
        return false;
    }

    if (touch.pressed && m_lyric_rect.Contains(touch.x, touch.y)
        && !kBtnSync.Contains(touch.x, touch.y) && !kBtnDownload.Contains(touch.x, touch.y)
        && !kBtnLayout.Contains(touch.x, touch.y)
        && !kBtnOffsetMinus.Contains(touch.x, touch.y)
        && !kBtnOffsetPlus.Contains(touch.x, touch.y)
        && !kBtnVolumeMinus.Contains(touch.x, touch.y)
        && !kBtnVolumePlus.Contains(touch.x, touch.y)
        && !(FaceButtonsVisible(ctx)
             && (kBtnFaceA.Contains(touch.x, touch.y) || kBtnFaceB.Contains(touch.x, touch.y)
                 || kBtnFaceX.Contains(touch.x, touch.y) || kBtnFaceY.Contains(touch.x, touch.y))))
    {
        m_lyric_dragging = true;
    }

    if (m_lyric_dragging)
    {
        // 向下拖 = 往前翻，所以偏移直接跟着手指走
        m_lyric_browse_offset += touch.step_y;
        m_lyric_browse_hold = kLyricBrowseHold;

        if (!touch.touching)
        {
            m_lyric_dragging = false;
            FinishLyricBrowse(ctx, touch.IsDrag());
        }
        return true;
    }

    // 松手之后先停一会儿让人读完，再滑回当前播放的那句
    if (m_lyric_browse_hold > 0.0)
    {
        m_lyric_browse_hold -= delta_seconds;
    }
    else if (m_lyric_browse_offset != 0.0)
    {
        m_lyric_browse_offset *= std::pow(0.02, delta_seconds);
        if (std::fabs(m_lyric_browse_offset) < 1.0)
            m_lyric_browse_offset = 0.0;
    }
    return false;
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

    // ---- 按下态高亮 ----
    const bool face_visible = FaceButtonsVisible(ctx);
    const bool lyric_view = (m_view == VIEW_LYRIC);
    m_pressed_button = HIT_NONE;
    if (touch.touching)
    {
        if (kBtnPrev.Contains(touch.x, touch.y))          m_pressed_button = HIT_PREV;
        else if (kBtnPlay.Contains(touch.x, touch.y))     m_pressed_button = HIT_PLAY;
        else if (kBtnNext.Contains(touch.x, touch.y))     m_pressed_button = HIT_NEXT;
        else if (kBtnStop.Contains(touch.x, touch.y))     m_pressed_button = HIT_STOP;
        else if (kBtnDownload.Contains(touch.x, touch.y)) m_pressed_button = HIT_TOOL_DOWNLOAD;
        else if (kBtnVolumeMinus.Contains(touch.x, touch.y))
            m_pressed_button = HIT_VOLUME_MINUS;
        else if (kBtnVolumePlus.Contains(touch.x, touch.y))
            m_pressed_button = HIT_VOLUME_PLUS;
        else if (lyric_view && kBtnSync.Contains(touch.x, touch.y))
            m_pressed_button = HIT_TOOL_SYNC;
        else if (lyric_view && kBtnLayout.Contains(touch.x, touch.y))
            m_pressed_button = HIT_TOOL_LAYOUT;
        else if (lyric_view && kBtnOffsetMinus.Contains(touch.x, touch.y))
            m_pressed_button = HIT_OFFSET_MINUS;
        else if (lyric_view && kBtnOffsetPlus.Contains(touch.x, touch.y))
            m_pressed_button = HIT_OFFSET_PLUS;
        else if (face_visible)
        {
            if (kBtnFaceA.Contains(touch.x, touch.y))      m_pressed_button = HIT_FACE_A;
            else if (kBtnFaceB.Contains(touch.x, touch.y)) m_pressed_button = HIT_FACE_B;
            else if (kBtnFaceX.Contains(touch.x, touch.y)) m_pressed_button = HIT_FACE_X;
            else if (kBtnFaceY.Contains(touch.x, touch.y)) m_pressed_button = HIT_FACE_Y;
        }
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
    else if (kBtnDownload.Contains(touch.x, touch.y))
    {
        if (player.GetCurrentSong().file_path.empty())
            ctx.ShowToast("没有正在播放的曲目");
        else
            ctx.next_screen = SCREEN_DOWNLOAD;
    }
    else if (lyric_view && kBtnLayout.Contains(touch.x, touch.y))
    {
        CConfig& config = player.GetConfig();
        const bool two_column = !config.GetLyricTwoColumn();
        config.SetLyricTwoColumn(two_column);
        ctx.ShowToast(two_column ? "歌词改为双栏（左原文 右译文）" : "歌词改为单栏");
    }
    else if (kBtnVolumeMinus.Contains(touch.x, touch.y))
    {
        player.AdjustVolume(-kVolumeStep);
        m_volume_overlay_timer = 1.5;
    }
    else if (kBtnVolumePlus.Contains(touch.x, touch.y))
    {
        player.AdjustVolume(kVolumeStep);
        m_volume_overlay_timer = 1.5;
    }
    else if (lyric_view && kBtnOffsetMinus.Contains(touch.x, touch.y))
    {
        player.AdjustLyricOffset(-500);
        ctx.ShowToast("歌词提前 0.5 秒");
    }
    else if (lyric_view && kBtnOffsetPlus.Contains(touch.x, touch.y))
    {
        player.AdjustLyricOffset(500);
        ctx.ShowToast("歌词延后 0.5 秒");
    }
    else if (lyric_view && kBtnSync.Contains(touch.x, touch.y))
    {
        const bool sync = !player.GetConfig().GetLyricSeekSync();
        player.GetConfig().SetLyricSeekSync(sync);
        ctx.ShowToast(sync ? "拖动歌词将同时改变播放进度" : "拖动歌词只是翻看，松手后自动归位");
    }
    else if (face_visible && kBtnFaceA.Contains(touch.x, touch.y))
    {
        ActivateFaceButton(ctx, HIT_FACE_A);
    }
    else if (face_visible && kBtnFaceB.Contains(touch.x, touch.y))
    {
        ActivateFaceButton(ctx, HIT_FACE_B);
    }
    else if (face_visible && kBtnFaceX.Contains(touch.x, touch.y))
    {
        ActivateFaceButton(ctx, HIT_FACE_X);
    }
    else if (face_visible && kBtnFaceY.Contains(touch.x, touch.y))
    {
        ActivateFaceButton(ctx, HIT_FACE_Y);
    }
    else if (kCoverRect.Contains(touch.x, touch.y))
    {
        // 有封面就放大欣赏；没有封面时保留原来的"切换视图"行为，
        // 否则点在占位图上什么都不发生
        if (m_cover != nullptr)
            m_cover_fullscreen = true;
        else
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

    // 标题和艺术家用跑马灯：左栏只有 260 像素，中文歌名稍长就会被省略号切掉，
    // 而这两行恰恰是最需要看全的信息
    r.DrawTextMarquee(song.GetTitle(), kLeftX, kInfoY, kLeftWidth, CRenderer::FS_HUGE,
                      Theme::kText);
    r.DrawTextMarquee(song.GetArtist(), kLeftX, kInfoY + 46, kLeftWidth, CRenderer::FS_NORMAL,
                      Theme::kTextDim);
    if (!song.album.empty())
    {
        r.DrawTextEllipsis(song.album, kLeftX, kInfoY + 76, kLeftWidth, CRenderer::FS_SMALL,
                           Theme::kTextDisabled);
    }

}

void CPlayerScreen::DrawTrackCounter(ScreenContext& ctx)
{
    CPlayer& player = *ctx.player;
    const int total = player.GetPlaylistSize();
    if (total <= 0)
        return;

    // 放在右栏左下角。原来它占着左栏的一行，挪走之后那一行的高度给了封面。
    char fraction[32];
    std::snprintf(fraction, sizeof(fraction), "%d / %d", player.GetCurrentIndex() + 1, total);
    ctx.renderer->DrawText(fraction, kRightX + 4, kRightY + kRightHeight - 26,
                           CRenderer::FS_SMALL, Theme::kAccent);
}

void CPlayerScreen::DrawTransportButtons(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    const bool playing = ctx.player->IsPlaying();

    // 先画中心轴心，让四个按钮读起来是一个方向键而不是四个孤立的方块
    r.FillRoundRect(kCrossHub.x + 14, kCrossHub.y + 14, kCrossHub.w - 28, kCrossHub.h - 28, 4,
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
        r.FillRoundRect(button.rect.x, button.rect.y, button.rect.w, button.rect.h, 10,
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

void CPlayerScreen::DrawLyricTools(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    const bool sync = ctx.player->GetConfig().GetLyricSeekSync();

    const bool two_column = IsTwoColumnLyric(ctx);
    // 下载针对的是当前这首歌，和看的是哪个视图无关，所以哪儿都画；
    // 同步和单双栏只对歌词有意义。位置固定不变——按钮跟着视图挪位比留个空当更糟。
    const bool lyric_view = (m_view == VIEW_LYRIC);

    struct ToolDef { const Rect& rect; HitButton id; bool active; bool visible; };
    const ToolDef tools[] = {
        { kBtnDownload, HIT_TOOL_DOWNLOAD, false,      true },
        { kBtnSync,     HIT_TOOL_SYNC,     sync,       lyric_view },
        { kBtnLayout,   HIT_TOOL_LAYOUT,   two_column, lyric_view },
    };

    for (const ToolDef& tool : tools)
    {
        if (!tool.visible)
            continue;
        const bool pressed = (m_pressed_button == tool.id);
        Color background = Theme::kPanel.WithAlpha(200);
        if (pressed)
            background = Theme::kAccentDim;
        else if (tool.active)
            background = Theme::kAccent.WithAlpha(200);

        r.FillRoundRect(tool.rect.x, tool.rect.y, tool.rect.w, tool.rect.h, 8, background);

        // 图标用图形拼，不用字符：箭头这类符号未必在系统共享字体里有字形，
        // 缺了就是个豆腐块。走带按钮那边也是这么做的。
        const int cx = tool.rect.x + tool.rect.w / 2;
        const int cy = tool.rect.y + tool.rect.h / 2;
        const Color icon = Theme::kText;

        if (tool.id == HIT_TOOL_DOWNLOAD)
        {
            // 向下的箭头 + 底部托盘：下载到本地
            r.FillRect(cx - 2, cy - 12, 4, 11, icon);
            r.FillTriangle(cx - 7, cy - 2, cx + 7, cy - 2, cx, cy + 7, icon);
            r.FillRect(cx - 9, cy + 10, 18, 3, icon);
        }
        else if (tool.id == HIT_TOOL_LAYOUT)
        {
            // 一栏画成整块，两栏画成并排两块——图标本身就说明了它切换的是什么
            if (tool.active)
            {
                r.FillRect(cx - 10, cy - 8, 8, 16, icon);
                r.FillRect(cx + 2, cy - 8, 8, 16, icon);
            }
            else
            {
                r.FillRect(cx - 10, cy - 8, 20, 16, icon);
            }
        }
        else
        {
            // 一上一下两个箭头：拖歌词时进度跟着走
            r.FillTriangle(cx - 8, cy - 1, cx - 1, cy - 1, cx - 4, cy - 10, icon);
            r.FillRect(cx - 6, cy - 1, 4, 11, icon);
            r.FillTriangle(cx + 1, cy + 1, cx + 8, cy + 1, cx + 4, cy + 10, icon);
            r.FillRect(cx + 2, cy - 10, 4, 11, icon);
        }
    }
}

void CPlayerScreen::DrawStepButtons(ScreenContext& ctx, const Rect& minus, const Rect& plus,
                                    HitButton minus_id, HitButton plus_id)
{
    CRenderer& r = *ctx.renderer;

    struct StepDef { const Rect& rect; HitButton id; const char* sign; };
    const StepDef buttons[] = {
        { minus, minus_id, "−" },
        { plus,  plus_id,  "+" },
    };

    for (const StepDef& button : buttons)
    {
        const bool pressed = (m_pressed_button == button.id);
        r.FillRoundRect(button.rect.x, button.rect.y, button.rect.w, button.rect.h, 10,
                        pressed ? Theme::kAccentDim : Theme::kPanel.WithAlpha(200));
        r.DrawText(button.sign, button.rect.x + button.rect.w / 2, button.rect.y + 6,
                   CRenderer::FS_NORMAL, Theme::kText, CRenderer::ALIGN_CENTER);
    }
}

void CPlayerScreen::DrawStepTools(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;

    // ---- 歌词偏移。只有歌词视图下才有意义 ----
    if (m_view == VIEW_LYRIC)
    {
        DrawStepButtons(ctx, kBtnOffsetMinus, kBtnOffsetPlus, HIT_OFFSET_MINUS, HIT_OFFSET_PLUS);

        const int offset = ctx.player->GetLyricOffset();
        char offset_text[32];
        std::snprintf(offset_text, sizeof(offset_text), "%+.1f 秒", offset / 1000.0);
        r.DrawText(offset == 0 ? "歌词偏移" : offset_text,
                   (kBtnOffsetMinus.x + kBtnOffsetMinus.w + kBtnOffsetPlus.x) / 2,
                   kRightY + 10, CRenderer::FS_SMALL,
                   offset == 0 ? Theme::kTextDisabled : Theme::kHighlight,
                   CRenderer::ALIGN_CENTER);
    }

    // ---- 音量。和视图无关，哪个视图都要能调 ----
    DrawStepButtons(ctx, kBtnVolumeMinus, kBtnVolumePlus, HIT_VOLUME_MINUS, HIT_VOLUME_PLUS);

    // 喇叭用图形拼，不用字符：系统共享字体里未必有那个码点，缺了就是个豆腐块。
    // 中间这块只有 kVolumeLabelW 宽，所以图标靠左、数字紧跟其后，不居中排。
    const int volume = ctx.player->GetVolume();
    const int icon_x = kBtnVolumeMinus.x + kBtnVolumeMinus.w + 6;
    const int icon_y = kRightY + kToolSize / 2;
    const Color icon = (volume == 0) ? Theme::kTextDisabled : Theme::kTextDim;
    r.FillRect(icon_x, icon_y - 4, 5, 8, icon);
    r.FillTriangle(icon_x + 4, icon_y - 9, icon_x + 4, icon_y + 9, icon_x + 11, icon_y, icon);
    if (volume == 0)
        r.DrawLine(icon_x, icon_y + 9, icon_x + 13, icon_y - 9, Theme::kHighlight);

    // 不写百分号：这一栏窄，而且旁边就是加减按钮，数字是什么已经很清楚
    char volume_text[16];
    std::snprintf(volume_text, sizeof(volume_text), "%d", volume);
    r.DrawText(volume_text, icon_x + 18, kRightY + 10, CRenderer::FS_SMALL, Theme::kTextDim);
}

void CPlayerScreen::DrawFaceButtons(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    const bool playing = ctx.player->IsPlaying();

    struct FaceDef { const Rect& rect; HitButton id; const char* key; const char* label; };
    const FaceDef faces[] = {
        { kBtnFaceX, HIT_FACE_X, "X", "视图" },
        { kBtnFaceY, HIT_FACE_Y, "Y", "模式" },
        { kBtnFaceA, HIT_FACE_A, "A", playing ? "暂停" : "播放" },
        { kBtnFaceB, HIT_FACE_B, "B", "偏移" },
    };

    for (const FaceDef& face : faces)
    {
        const bool pressed = (m_pressed_button == face.id);
        const int cx = face.rect.x + face.rect.w / 2;
        // 圆形按钮画不了，就用一个接近正方形的大圆角矩形代替，视觉上足够像手柄按键
        const int d = 40;
        r.FillRoundRect(cx - d / 2, face.rect.y + 2, d, d, d / 2,
                        pressed ? Theme::kAccentDim : Theme::kPanel.WithAlpha(210));
        r.DrawText(face.key, cx, face.rect.y + 5, CRenderer::FS_NORMAL, Theme::kText,
                   CRenderer::ALIGN_CENTER);
        r.DrawText(face.label, cx, face.rect.y + 42, CRenderer::FS_SMALL, Theme::kTextDim,
                   CRenderer::ALIGN_CENTER);
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

void CPlayerScreen::DrawLyricText(ScreenContext& ctx, const CLrcParser::Lyric& line,
                                  int center_x, int y, int max_width, bool is_current,
                                  int position)
{
    CRenderer& r = *ctx.renderer;
    CRenderer::FontSize font = is_current ? CRenderer::FS_LYRIC_CURRENT : CRenderer::FS_LYRIC;

    if (!is_current || line.split.empty())
    {
        Color color = is_current ? Theme::kLyricCurrent : Theme::kLyricOther;
        r.DrawTextEllipsis(line.text, center_x, y, max_width, font, color,
                           CRenderer::ALIGN_CENTER);
        return;
    }

    // 逐字歌词：先画底色整行，再用当前进度裁出已唱的部分覆盖上去
    int text_w = 0, text_h = 0;
    r.MeasureText(line.text, font, text_w, text_h);
    int text_x = center_x - text_w / 2;

    r.DrawText(line.text, text_x, y, font, Theme::kLyricOther);

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
        r.DrawText(line.text.substr(0, sung_bytes), text_x, y, font, Theme::kLyricKaraoke);
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
        r.DrawText("把同名 .lrc 文件放在歌曲旁边，或按下右摇杆在线下载",
                   x + width / 2, y + height / 2 + 20,
                   CRenderer::FS_SMALL, Theme::kTextDisabled, CRenderer::ALIGN_CENTER);
        return;
    }

    const std::vector<CLrcParser::Lyric>& lines = lyrics.GetLyrics();
    const int position = player.GetPosition();
    const int current = lyrics.GetLyricIndex(position);
    const bool show_translation = player.GetConfig().GetShowTranslation();

    // 双栏只在真的有译文时才启用，否则右边是一片空白，反而更难看
    bool has_translation = false;
    for (const CLrcParser::Lyric& line : lines)
    {
        if (!line.translate.empty())
        {
            has_translation = true;
            break;
        }
    }
    const bool two_column = player.GetConfig().GetLyricTwoColumn() && show_translation
                            && has_translation;
    (void)has_translation;

    const int column_gap = 24;
    const int column_width = two_column ? (width - column_gap) / 2 : width;
    const int left_center = two_column ? x + column_width / 2 : x + width / 2;
    const int right_center = x + column_width + column_gap + column_width / 2;

    // 一条歌词折行后可能占好几行，所以行高不能是固定值：
    // 先把每条的折行结果和高度算出来，再按高度依次堆叠。
    struct Entry
    {
        int index;
        std::vector<std::string> original;
        std::vector<std::string> translation;
        int text_line_height;
        int height;
    };

    const int kEntryGap = 14;
    // 当前行上下各准备这么多条，够铺满一屏。手指把歌词拖出去多远，
    // 就得多备多少条，否则拖到头会看见空白。
    const int kMaxAround = 6 + std::min(120, static_cast<int>(std::fabs(m_lyric_browse_offset) / 36));

    auto build_entry = [&](int index) {
        Entry entry;
        entry.index = index;
        const CLrcParser::Lyric& line = lines[index];
        const bool is_current = (index == current);
        CRenderer::FontSize font = is_current ? CRenderer::FS_LYRIC_CURRENT : CRenderer::FS_LYRIC;

        entry.text_line_height = r.GetLineHeight(font) + 4;
        entry.original = r.WrapText(line.text, font, column_width);

        int lines_count = static_cast<int>(entry.original.size());
        if (!line.translate.empty() && show_translation)
        {
            CRenderer::FontSize tfont = two_column ? font : CRenderer::FS_SMALL;
            entry.translation = r.WrapText(line.translate, tfont, column_width);
            if (two_column)
            {
                // 左右并排，条目高度取两栏中较高的那个
                lines_count = std::max(lines_count, static_cast<int>(entry.translation.size()));
            }
            else
            {
                // 单栏时译文排在原文下方，高度要累加，否则会压到下一条上
                entry.height = lines_count * entry.text_line_height
                             + static_cast<int>(entry.translation.size())
                               * (r.GetLineHeight(CRenderer::FS_SMALL) + 2);
                return entry;
            }
        }
        entry.height = lines_count * entry.text_line_height;
        return entry;
    };

    std::vector<Entry> entries;
    const int first = std::max(0, current - kMaxAround);
    const int last = std::min(static_cast<int>(lines.size()) - 1, current + kMaxAround);
    for (int i = first; i <= last; ++i)
        entries.push_back(build_entry(i));

    // 当前行居中；切行时用 m_lyric_scroll 做一点位移动画
    int current_pos = current - first;
    if (current_pos < 0 || current_pos >= static_cast<int>(entries.size()))
        current_pos = 0;

    const double animation_offset = m_lyric_scroll * (entries[current_pos].height + kEntryGap);
    int current_top = y + height / 2 - entries[current_pos].height / 2
                    + static_cast<int>(animation_offset + m_lyric_browse_offset);

    // 由当前条目向上下推算出每条的顶端 y
    std::vector<int> tops(entries.size(), 0);
    tops[current_pos] = current_top;
    for (int i = current_pos - 1; i >= 0; --i)
        tops[i] = tops[i + 1] - entries[i].height - kEntryGap;
    for (size_t i = current_pos + 1; i < entries.size(); ++i)
        tops[i] = tops[i - 1] + entries[i - 1].height + kEntryGap;

    // 记下落在视图中心的那句：松手时"同步进度"要跳到它上面去
    const int view_center = y + height / 2;
    m_lyric_browse_index = -1;
    int best_distance = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const int entry_center = tops[i] + entries[i].height / 2;
        const int distance = std::abs(entry_center - view_center);
        if (m_lyric_browse_index < 0 || distance < best_distance)
        {
            best_distance = distance;
            m_lyric_browse_index = entries[i].index;
        }
    }

    r.PushClip(x, y, width, height);

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const Entry& entry = entries[i];
        int top = tops[i];
        if (top + entry.height < y || top > y + height)
            continue;                       // 完全在可视区外，不必绘制

        const CLrcParser::Lyric& line = lines[entry.index];
        const bool is_current = (entry.index == current);
        CRenderer::FontSize font = is_current ? CRenderer::FS_LYRIC_CURRENT : CRenderer::FS_LYRIC;
        Color color = is_current ? Theme::kLyricCurrent : Theme::kLyricOther;

        // 逐字高亮只在原文没被折行时做：折行之后按字节比例算高亮位置
        // 会跨行错位，与其画错不如退回整行高亮。
        if (is_current && !line.split.empty() && entry.original.size() == 1)
        {
            DrawLyricText(ctx, line, left_center, top, column_width, true, position);
        }
        else
        {
            for (size_t k = 0; k < entry.original.size(); ++k)
            {
                r.DrawText(entry.original[k], left_center,
                           top + static_cast<int>(k) * entry.text_line_height, font, color,
                           CRenderer::ALIGN_CENTER);
            }
        }

        if (entry.translation.empty())
            continue;

        if (two_column)
        {
            Color tcolor = is_current ? Theme::kLyricTranslate : Theme::kLyricOther;
            for (size_t k = 0; k < entry.translation.size(); ++k)
            {
                r.DrawText(entry.translation[k], right_center,
                           top + static_cast<int>(k) * entry.text_line_height, font, tcolor,
                           CRenderer::ALIGN_CENTER);
            }
        }
        else
        {
            const int sub_h = r.GetLineHeight(CRenderer::FS_SMALL) + 2;
            int ty = top + static_cast<int>(entry.original.size()) * entry.text_line_height;
            Color tcolor = is_current ? Theme::kLyricTranslate : Theme::kLyricOther;
            for (size_t k = 0; k < entry.translation.size(); ++k)
            {
                r.DrawText(entry.translation[k], left_center,
                           ty + static_cast<int>(k) * sub_h, CRenderer::FS_SMALL, tcolor,
                           CRenderer::ALIGN_CENTER);
            }
        }
    }

    r.PopClip();

    // 正在拖动歌词时给出反馈：中心线 + 那句的时间。
    // 没有这条线的话，"松手会跳到哪一句"全靠猜。
    if ((m_lyric_dragging || m_lyric_stick_browsing) && m_lyric_browse_index >= 0)
    {
        const bool sync = player.GetConfig().GetLyricSeekSync();
        const Color line_color = sync ? Theme::kAccent : Theme::kSeparator;
        r.DrawLine(x, view_center, x + 40, view_center, line_color);
        r.DrawLine(x + width - 40, view_center, x + width, view_center, line_color);

        if (sync && m_lyric_browse_index < static_cast<int>(lines.size()))
        {
            CPlayTime target{ lines[m_lyric_browse_index].time_start };
            r.DrawText(target.toString(false), x + width - 4, view_center - 26,
                       CRenderer::FS_SMALL, Theme::kAccent, CRenderer::ALIGN_RIGHT);
        }
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

void CPlayerScreen::DrawCoverView(ScreenContext& ctx, int x, int y, int width, int height)
{
    CRenderer& r = *ctx.renderer;

    if (m_cover == nullptr)
    {
        r.DrawText("这首歌没有封面", x + width / 2, y + height / 2 - 20, CRenderer::FS_LARGE,
                   Theme::kTextDisabled, CRenderer::ALIGN_CENTER);
        r.DrawText("按下右摇杆可以在线下载", x + width / 2, y + height / 2 + 20,
                   CRenderer::FS_SMALL, Theme::kTextDisabled, CRenderer::ALIGN_CENTER);
        return;
    }

    // 封面基本都是 1:1，按短边取正方形铺满
    const int size = std::min(width, height) - 16;
    const int cx = x + (width - size) / 2;
    const int cy = y + (height - size) / 2;
    r.DrawTexture(m_cover, cx, cy, size, size);
    r.DrawRect(cx, cy, size, size, Theme::kSeparator);
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

void CPlayerScreen::DrawCoverFullscreen(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;

    r.FillRect(0, 0, Theme::kScreenWidth, Theme::kScreenHeight, Color{ 0, 0, 0, 240 });

    // 顶栏底栏这一帧不画（WantsFullScreen），所以整块屏幕都能用。
    // 按短边铺满并保持正方形：封面基本都是 1:1。
    const int size = Theme::kScreenHeight - Theme::kPadding;
    const int cx = (Theme::kScreenWidth - size) / 2;
    const int cy = (Theme::kScreenHeight - size) / 2;
    r.DrawTexture(m_cover, cx, cy, size, size);
    r.DrawRect(cx, cy, size, size, Theme::kSeparator);

    // 曲名压在图片下沿，顺带提示怎么退出
    const SongInfo& song = ctx.player->GetCurrentSong();
    r.DrawTextEllipsis(song.GetDisplayName(), Theme::kScreenWidth / 2,
                       Theme::kScreenHeight - 30, Theme::kScreenWidth - 200,
                       CRenderer::FS_SMALL, Theme::kTextDim, CRenderer::ALIGN_CENTER);
    r.DrawText("点一下或按 B 返回", Theme::kScreenWidth - Theme::kPadding, 12,
               CRenderer::FS_SMALL, Theme::kTextDisabled, CRenderer::ALIGN_RIGHT);
}

void CPlayerScreen::Draw(ScreenContext& ctx)
{
    if (m_cover_fullscreen && m_cover != nullptr)
    {
        DrawCoverFullscreen(ctx);
        return;
    }

    // 左侧：封面 + 曲目信息 + 走带按钮 + 进度条；右侧：歌词或频谱
    DrawCover(ctx);
    DrawSongInfo(ctx);
    DrawTransportButtons(ctx);
    DrawProgressBar(ctx);
    DrawTrackCounter(ctx);

    // 触摸判定用的是这一帧记下的矩形，下一帧才生效；首帧为空不会误命中
    m_lyric_rect = Rect{ kRightX, kRightY, kRightWidth, kRightHeight };

    if (m_view == VIEW_LYRIC)
        DrawLyricView(ctx, kRightX, kRightY, kRightWidth, kRightHeight);
    else if (m_view == VIEW_COVER)
        DrawCoverView(ctx, kRightX, kRightY, kRightWidth, kRightHeight);
    else
        DrawSpectrumView(ctx, kRightX, kRightY, kRightWidth, kRightHeight);

    // 工具区画在视图之上，且不分视图：音量和下载在哪个视图下都得点得到
    DrawLyricTools(ctx);
    DrawStepTools(ctx);

    if (FaceButtonsVisible(ctx))
        DrawFaceButtons(ctx);

    DrawVolumeOverlay(ctx);
}
