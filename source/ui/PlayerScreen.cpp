#include "PlayerScreen.h"
#include "Renderer.h"
#include "../Player.h"
#include "../core/Config.h"
#include "../core/FileUtil.h"
#include "../input/InputMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    const int kSeekStep = 5000;             // ZL/ZR 快进快退步长（毫秒）
    const int kFineSeekStep = 1000;         // 左摇杆左右：逐秒微调
    const int kVolumeStep = 5;

    // 逻辑分辨率固定 1280x720。
    //
    // 内容区（封面、曲目信息、进度条、歌词区）的尺寸随模式变，见 ComputeLayout：
    // 沉浸模式下没有那些触摸按钮，左栏可以让封面占得更大。
    // 而按钮簇的位置必须是编译期常量——绘制和命中判定共用同一份坐标，
    // 一旦画一套、点另一套就很难查。它们只在常规模式下出现，所以按常规分栏算。
    const int kLeftX = 48;
    const int kNormalLeftWidth = 260;
    const int kNormalRightX = kLeftX + kNormalLeftWidth + Theme::kPadding * 2;
    const int kNormalRightWidth = Theme::kScreenWidth - kNormalRightX - Theme::kPadding * 2;
    const int kContentTop = Theme::kHeaderHeight + 16;

    // 走带按钮排成十字，位置与方向键一一对应：
    //        [上] 播放/暂停
    //  [左]        [右]      上一曲 / 下一曲
    //        [下] 停止
    // 屏幕上的布局本身就是键位说明，所以底栏不再重复方向键的指引。
    // 做成正方形：方向键本来就是四个等大的键，扁矩形读起来不像。
    const int kBtnSize = 48;
    const int kBtnGap = 5;
    const int kCrossX = kLeftX + (kNormalLeftWidth - (kBtnSize * 3 + kBtnGap * 2)) / 2;
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

    const int kProgressH = 6;

    // 歌词区右上角一排小按钮：下载、拖歌词是否带进度、单栏/双栏。
    // 歌词偏移不在这里——它只保留"按住 B + 方向键左右"这一条路，
    // 屏幕上不给它位置。
    const int kToolSize = 40;
    const int kToolGap = 8;
    const int kToolStep = kToolSize + kToolGap;
    //
    // 四个按钮排成一横排（1×4）而不是 4×1 或 2×2：歌词是垂直居中的，
    // 顶上一条细横带侵占最少；竖排贴着右边缘会和双栏歌词的右列打架，
    // 2×2 则是个更高的方块，往歌词里伸得更多。
    const Rect kBtnRepeat{ kNormalRightX + kNormalRightWidth - kToolSize, kContentTop,
                           kToolSize, kToolSize };
    const Rect kBtnLayout{ kBtnRepeat.x - kToolStep, kContentTop, kToolSize, kToolSize };
    const Rect kBtnSync{ kBtnLayout.x - kToolStep, kContentTop, kToolSize, kToolSize };
    const Rect kBtnDownload{ kBtnSync.x - kToolStep, kContentTop, kToolSize, kToolSize };

    // 歌词区左上角：左摇杆的十字。
    //
    //          [+] 音量加
    //   [←] 退1秒   [→] 进1秒
    //          [−] 音量减
    //
    // 形状照着方向键十字和 ABXY 菱形来，摆的就是左摇杆本身的四个方向：
    // 上下调音量、左右逐秒进退。中心那格显示当前音量。
    //
    // 歌词偏移没有放进这个十字——左摇杆左右是逐秒进退，把偏移画在这个位置
    // 等于把键位教错了。偏移归到右上角那排，和同样"针对当前这首歌"的
    // 下载、同步、单双栏放在一起。
    const int kStickCell = 48;
    const int kStickGap = 5;
    const int kStickX = kNormalRightX;
    const int kStickY = kContentTop;
    const int kStickColMid = kStickX + kStickCell + kStickGap;
    const int kStickRowMid = kStickY + kStickCell + kStickGap;
    const int kStickRowBottom = kStickRowMid + kStickCell + kStickGap;

    const Rect kBtnVolumePlus{ kStickColMid, kStickY, kStickCell, kStickCell };
    const Rect kBtnSeekBack{ kStickX, kStickRowMid, kStickCell, kStickCell };
    const Rect kBtnSeekForward{ kStickColMid + kStickCell + kStickGap, kStickRowMid,
                                kStickCell, kStickCell };
    const Rect kBtnVolumeMinus{ kStickColMid, kStickRowBottom, kStickCell, kStickCell };
    const Rect kStickHub{ kStickColMid, kStickRowMid, kStickCell, kStickCell };

    // 右下角的 ABXY，按手柄上的实际方位摆成菱形：
    //          [X] 视图
    //   [Y] 模式     [A] 播放
    //          [B] 偏移
    // 每格里上面是按键字母、下面是功能名，点一下等同按那个键。
    const int kFaceCellW = 62;
    const int kFaceCellH = 62;
    const int kFaceRight = kNormalRightX + kNormalRightWidth;
    const int kFaceBottom = Theme::kScreenHeight - Theme::kFooterHeight - 16;
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

CPlayerScreen::Layout CPlayerScreen::ComputeLayout(bool immersive, bool hide_hints)
{
    Layout layout;

    // 沉浸模式下左栏加宽：那些触摸按钮都不画了，省下来的地方给封面。
    layout.left_width = immersive ? 380 : kNormalLeftWidth;

    const int cover_size = immersive ? 340 : 226;
    layout.cover = Rect{ kLeftX + (layout.left_width - cover_size) / 2, 88,
                         cover_size, cover_size };
    layout.info_y = layout.cover.y + layout.cover.h + 16;

    layout.progress_y = 592;
    layout.progress_hit = Rect{ kLeftX - 12, layout.progress_y - 20,
                                layout.left_width + 24, 44 };

    const int right_x = kLeftX + layout.left_width + Theme::kPadding * 2;
    // 底栏提示藏起来时，内容区顺势往下长满
    const int bottom = Theme::kScreenHeight - (hide_hints ? 0 : Theme::kFooterHeight) - 16;
    layout.right = Rect{ right_x, kContentTop,
                         Theme::kScreenWidth - right_x - Theme::kPadding * 2,
                         bottom - kContentTop };
    return layout;
}

void CPlayerScreen::ReleaseResources(ScreenContext& ctx)
{
    if (m_cover != nullptr)
    {
        ctx.renderer->FreeTexture(m_cover);
        m_cover = nullptr;
    }
    if (m_background != nullptr)
    {
        ctx.renderer->FreeTexture(m_background);
        m_background = nullptr;
    }
    m_background_mode = -1;
    m_cover_source.clear();
}

void CPlayerScreen::RefreshBackground(ScreenContext& ctx)
{
    const int mode = static_cast<int>(ctx.player->GetConfig().GetLyricBackground());
    if (mode == m_background_mode)
        return;
    m_background_mode = mode;

    if (m_background != nullptr)
    {
        ctx.renderer->FreeTexture(m_background);
        m_background = nullptr;
    }
    // 只有"自定义图片"要自己读文件；用封面当背景时直接复用 m_cover
    if (mode == CConfig::LB_FILE)
    {
        m_background = ctx.renderer->LoadImageFile(
            FileUtil::Combine(CPlayer::GetDataDir(), "background.jpg"));
        if (m_background == nullptr)
        {
            m_background = ctx.renderer->LoadImageFile(
                FileUtil::Combine(CPlayer::GetDataDir(), "background.png"));
        }
    }
}

void CPlayerScreen::DrawLyricBackground(ScreenContext& ctx, int x, int y, int width, int height)
{
    const CConfig::LyricBackground mode = ctx.player->GetConfig().GetLyricBackground();
    if (mode == CConfig::LB_NONE)
        return;

    SDL_Texture* texture = (mode == CConfig::LB_COVER) ? m_cover : m_background;
    if (texture == nullptr)
        return;

    CRenderer& r = *ctx.renderer;
    r.PushClip(x, y, width, height);
    // 等比填满并裁切，别把封面拉成长方形
    r.DrawTextureCover(texture, x, y, width, height, 70);
    // 再压一层暗色。背景图再淡，白色歌词落在浅色区域上也会糊；
    // 这一层保证对比度，代价只是背景更暗一点。
    r.FillRect(x, y, width, height, Color{ 0x1F, 0x1F, 0x27, 150 });
    r.PopClip();
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
    // 左摇杆的功能已经由歌词区左上角那个十字画出来了，这里不再重复。
    // 走带控制同理（左下角十字），ABXY 同理（右下角菱形）。
    return "ZL/ZR ±5秒|右摇杆↑↓ 翻歌词|L 封面大小|R 单栏/双栏|LS 触摸|RS 下载|"
           "B×2 退出|− 列表|＋ 设置";
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

    // 布局每帧算一次，Update 里的命中判定和 Draw 用的是同一份
    m_layout = ComputeLayout(!input.IsTouchEnabled(),
                             player.GetConfig().GetHideHints());

    RefreshCover(ctx);
    RefreshBackground(ctx);

    // 封面全屏查看时只接受"退出"，避免误触其它功能
    if (m_cover_fullscreen)
    {
        const CInputMap::TouchState& touch = input.GetTouch();
        if (input.IsDown(CInputMap::BTN_B) || input.IsDown(CInputMap::BTN_A)
            || input.IsDown(CInputMap::BTN_L)
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

    // L / R 不再是上/下一曲——那两件事方向键和屏幕上的十字都能做。
    // 换成两个原本只有触摸能触发的功能，这样关掉触摸之后它们也还在。
    if (input.IsDown(CInputMap::BTN_L))
    {
        if (m_cover != nullptr)
            m_cover_fullscreen = !m_cover_fullscreen;
        else
            ctx.ShowToast("这首歌没有封面");
    }
    if (input.IsDown(CInputMap::BTN_R))
    {
        CConfig& config = player.GetConfig();
        const bool two_column = !config.GetLyricTwoColumn();
        config.SetLyricTwoColumn(two_column);
        ctx.ShowToast(two_column ? "歌词改为双栏（左原文 右译文）" : "歌词改为单栏");
    }

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
    // 触摸没按住任何按钮时，让实体键来点亮对应的那个
    HighlightHeldButton(ctx);

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

    // 按住 B + 方向键左右：调整歌词偏移。默认关闭，见设置里的"歌词时间偏移"。
    // 这里必须用只认方向键的 BTN_DPAD_*，因为摇杆左右已经分给逐秒快进退了
    const bool offset_enabled = player.GetConfig().GetLyricOffsetEnabled();
    if (offset_enabled && input.IsHeld(CInputMap::BTN_B))
    {
        if (input.IsRepeat(CInputMap::BTN_DPAD_RIGHT))
        {
            player.AdjustLyricOffset(500);
            ctx.ShowToast("歌词延后 0.5 秒");
            m_exit_prompt_timer = 0.0;      // 这次 B 是当修饰键用的，不算退出意图
        }
        if (input.IsRepeat(CInputMap::BTN_DPAD_LEFT))
        {
            player.AdjustLyricOffset(-500);
            ctx.ShowToast("歌词提前 0.5 秒");
            m_exit_prompt_timer = 0.0;
        }
    }

    // 连按两次 B 退出程序。播放界面是根界面，B 在这里本来没有返回的含义。
    if (input.IsDown(CInputMap::BTN_B))
    {
        if (m_exit_prompt_timer > 0.0)
        {
            ctx.request_exit = true;
        }
        else
        {
            m_exit_prompt_timer = 1.5;
            ctx.ShowToast("再按一次 B 退出");
        }
    }
    if (m_exit_prompt_timer > 0.0)
        m_exit_prompt_timer -= delta_seconds;

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
        // 和按实体 B 键走同一条路：点两次才退出
        if (m_exit_prompt_timer > 0.0)
        {
            ctx.request_exit = true;
        }
        else
        {
            m_exit_prompt_timer = 1.5;
            ctx.ShowToast("再点一次 B 退出");
        }
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
        && !kBtnRepeat.Contains(touch.x, touch.y)
        && !kBtnVolumeMinus.Contains(touch.x, touch.y)
        && !kBtnVolumePlus.Contains(touch.x, touch.y)
        && !kBtnSeekBack.Contains(touch.x, touch.y)
        && !kBtnSeekForward.Contains(touch.x, touch.y)
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

void CPlayerScreen::HighlightHeldButton(ScreenContext& ctx)
{
    // 手指按着的优先：那是用户正在直接操作的东西
    if (m_pressed_button != HIT_NONE)
        return;

    const CInputMap& input = *ctx.input;
    struct KeyMap { CInputMap::Button key; HitButton hit; };
    static const KeyMap kMap[] = {
        { CInputMap::BTN_A,           HIT_FACE_A },
        { CInputMap::BTN_B,           HIT_FACE_B },
        { CInputMap::BTN_X,           HIT_FACE_X },
        { CInputMap::BTN_Y,           HIT_FACE_Y },
        { CInputMap::BTN_DPAD_UP,     HIT_PLAY },
        { CInputMap::BTN_DPAD_DOWN,   HIT_STOP },
        { CInputMap::BTN_DPAD_LEFT,   HIT_PREV },
        { CInputMap::BTN_DPAD_RIGHT,  HIT_NEXT },
        { CInputMap::BTN_STICK_UP,    HIT_VOLUME_PLUS },
        { CInputMap::BTN_STICK_DOWN,  HIT_VOLUME_MINUS },
        { CInputMap::BTN_STICK_LEFT,  HIT_SEEK_BACK },
        { CInputMap::BTN_STICK_RIGHT, HIT_SEEK_FORWARD },
        { CInputMap::BTN_STICK_R,     HIT_TOOL_DOWNLOAD },
        { CInputMap::BTN_R,           HIT_TOOL_LAYOUT },
    };

    for (const KeyMap& entry : kMap)
    {
        if (input.IsHeld(entry.key))
        {
            m_pressed_button = entry.hit;
            return;
        }
    }
}

void CPlayerScreen::HandleTouch(ScreenContext& ctx)
{
    const CInputMap::TouchState& touch = ctx.input->GetTouch();
    CPlayer& player = *ctx.player;

    // ---- 进度条拖动 ----
    if (touch.pressed && m_layout.progress_hit.Contains(touch.x, touch.y)
        && player.GetLength() > 0)
        m_touch_seeking = true;

    if (m_touch_seeking)
    {
        double ratio = Clamp01(static_cast<double>(touch.x - kLeftX) / m_layout.left_width);
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
        else if (kBtnRepeat.Contains(touch.x, touch.y))   m_pressed_button = HIT_TOOL_REPEAT;
        else if (kBtnVolumeMinus.Contains(touch.x, touch.y))
            m_pressed_button = HIT_VOLUME_MINUS;
        else if (kBtnVolumePlus.Contains(touch.x, touch.y))
            m_pressed_button = HIT_VOLUME_PLUS;
        else if (kBtnSeekBack.Contains(touch.x, touch.y))
            m_pressed_button = HIT_SEEK_BACK;
        else if (kBtnSeekForward.Contains(touch.x, touch.y))
            m_pressed_button = HIT_SEEK_FORWARD;
        else if (lyric_view && kBtnSync.Contains(touch.x, touch.y))
            m_pressed_button = HIT_TOOL_SYNC;
        else if (lyric_view && kBtnLayout.Contains(touch.x, touch.y))
            m_pressed_button = HIT_TOOL_LAYOUT;
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
    else if (kBtnRepeat.Contains(touch.x, touch.y))
    {
        player.SwitchRepeatMode();
        ctx.ShowToast(CPlayer::GetRepeatModeName(player.GetRepeatMode()));
    }
    else if (lyric_view && kBtnLayout.Contains(touch.x, touch.y))
    {
        CConfig& config = player.GetConfig();
        const bool two_column = !config.GetLyricTwoColumn();
        config.SetLyricTwoColumn(two_column);
        ctx.ShowToast(two_column ? "歌词改为双栏（左原文 右译文）" : "歌词改为单栏");
    }
    else if (kBtnSeekBack.Contains(touch.x, touch.y))
    {
        player.SeekRelative(-kFineSeekStep);
    }
    else if (kBtnSeekForward.Contains(touch.x, touch.y))
    {
        player.SeekRelative(kFineSeekStep);
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
    else if (m_layout.cover.Contains(touch.x, touch.y))
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
    const int x = m_layout.cover.x, y = m_layout.cover.y, size = m_layout.cover.w;

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
        r.DrawText("没有正在播放的曲目", kLeftX, m_layout.info_y, CRenderer::FS_LARGE,
                   Theme::kTextDim);
        r.DrawText("按 ＋ 浏览 SD 卡上的音乐", kLeftX, m_layout.info_y + 44, CRenderer::FS_NORMAL,
                   Theme::kTextDisabled);
        return;
    }

    // 标题和艺术家用跑马灯：左栏只有 260 像素，中文歌名稍长就会被省略号切掉，
    // 而这两行恰恰是最需要看全的信息
    r.DrawTextMarquee(song.GetTitle(), kLeftX, m_layout.info_y, m_layout.left_width,
                      CRenderer::FS_HUGE,
                      Theme::kText);
    r.DrawTextMarquee(song.GetArtist(), kLeftX, m_layout.info_y + 46, m_layout.left_width,
                      CRenderer::FS_NORMAL,
                      Theme::kTextDim);
    if (!song.album.empty())
    {
        r.DrawTextEllipsis(song.album, kLeftX, m_layout.info_y + 76, m_layout.left_width,
                           CRenderer::FS_SMALL,
                           Theme::kTextDisabled);
    }

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

void CPlayerScreen::DrawRepeatIcon(ScreenContext& ctx, int cx, int cy, Color color,
                                   Color background)
{
    CRenderer& r = *ctx.renderer;

    // 画一个带箭头的循环框。列表循环和单曲循环共用它，区别只在中间那个 1。
    // 用圆角描边而不是直角矩形：同一屏上的按钮、面板都是圆角的，
    // 直角框夹在中间显得生硬。
    auto draw_loop = [&]() {
        r.DrawRoundRect(cx - 11, cy - 8, 22, 16, 5, 2, color, background);
        r.FillTriangle(cx + 5, cy - 12, cx + 5, cy - 4, cx + 13, cy - 8, color);
    };

    switch (ctx.player->GetRepeatMode())
    {
    case CConfig::RM_PLAY_ORDER:
        // 一条向右的箭头：从头播到尾
        r.FillRect(cx - 11, cy - 2, 16, 4, color);
        r.FillTriangle(cx + 4, cy - 8, cx + 4, cy + 8, cx + 13, cy, color);
        break;
    case CConfig::RM_PLAY_SHUFFLE:
        // 两条交叉的线 + 箭头：随机
        r.DrawLine(cx - 12, cy - 6, cx + 6, cy + 6, color);
        r.DrawLine(cx - 12, cy + 6, cx + 6, cy - 6, color);
        r.FillTriangle(cx + 4, cy - 10, cx + 4, cy - 2, cx + 12, cy - 6, color);
        r.FillTriangle(cx + 4, cy + 2, cx + 4, cy + 10, cx + 12, cy + 6, color);
        break;
    case CConfig::RM_LOOP_TRACK:
        draw_loop();
        r.DrawText("1", cx, cy - 11, CRenderer::FS_SMALL, color, CRenderer::ALIGN_CENTER);
        break;
    case CConfig::RM_PLAY_TRACK:
        // 箭头撞上一堵墙：放完这一首就停
        r.FillRect(cx - 11, cy - 2, 12, 4, color);
        r.FillTriangle(cx, cy - 8, cx, cy + 8, cx + 8, cy, color);
        r.FillRect(cx + 9, cy - 8, 3, 16, color);
        break;
    default:                                    // RM_LOOP_PLAYLIST
        draw_loop();
        break;
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
        { kBtnDownload,    HIT_TOOL_DOWNLOAD, false,      true },
        { kBtnSync,        HIT_TOOL_SYNC,     sync,       lyric_view },
        { kBtnLayout,      HIT_TOOL_LAYOUT,   two_column, lyric_view },
        { kBtnRepeat,      HIT_TOOL_REPEAT,   false,      true },
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
        else if (tool.id == HIT_TOOL_REPEAT)
        {
            DrawRepeatIcon(ctx, cx, cy, icon, background);
        }
        else if (tool.id == HIT_TOOL_LAYOUT)
        {
            // 直接写 1C / 2C。两块方形和一块方形的图形差别太小，
            // 在 40 像素的按钮里根本分不出来。
            r.DrawText(tool.active ? "2C" : "1C", cx, tool.rect.y + 8,
                       CRenderer::FS_SMALL, Theme::kText, CRenderer::ALIGN_CENTER);
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

    // 歌词偏移不再占屏幕上的位置。改过之后在这里标一行，
    // 免得用户按了 B + 方向键却看不出发生了什么。
    const int offset = ctx.player->GetLyricOffset();
    if (lyric_view && offset != 0)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "歌词 %+.1f 秒", offset / 1000.0);
        r.DrawText(text, kBtnDownload.x - 16, kContentTop + 10, CRenderer::FS_SMALL,
                   Theme::kHighlight, CRenderer::ALIGN_RIGHT);
    }
}

void CPlayerScreen::DrawStickCross(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    const int volume = ctx.player->GetVolume();

    // 中心轴心：先画它，四个方向键才读起来是一个整体而不是四个孤立的方块
    r.FillRoundRect(kStickHub.x + 6, kStickHub.y + 6, kStickHub.w - 12, kStickHub.h - 12, 6,
                    Theme::kPanel.WithAlpha(150));

    struct StickDef { const Rect& rect; HitButton id; };
    const StickDef buttons[] = {
        { kBtnVolumePlus,   HIT_VOLUME_PLUS },
        { kBtnSeekBack,     HIT_SEEK_BACK },
        { kBtnSeekForward,  HIT_SEEK_FORWARD },
        { kBtnVolumeMinus,  HIT_VOLUME_MINUS },
    };

    for (const StickDef& button : buttons)
    {
        const bool pressed = (m_pressed_button == button.id);
        r.FillRoundRect(button.rect.x, button.rect.y, button.rect.w, button.rect.h, 10,
                        pressed ? Theme::kAccentDim : Theme::kPanel.WithAlpha(200));

        const int cx = button.rect.x + button.rect.w / 2;
        const int cy = button.rect.y + button.rect.h / 2;
        const Color icon = Theme::kText;

        // 图标一律用图形拼，不用字符：箭头这些码点在系统共享字体里未必有。
        switch (button.id)
        {
        case HIT_VOLUME_PLUS:
            r.FillRect(cx - 9, cy - 2, 18, 4, icon);
            r.FillRect(cx - 2, cy - 9, 4, 18, icon);
            break;
        case HIT_VOLUME_MINUS:
            r.FillRect(cx - 9, cy - 2, 18, 4, icon);
            break;
        case HIT_SEEK_BACK:
            // 向左的三角 + 竖线，和走带按钮的"上一曲"区分开：这里没有竖线在外侧
            r.FillTriangle(cx + 6, cy - 9, cx + 6, cy + 9, cx - 7, cy, icon);
            break;
        case HIT_SEEK_FORWARD:
            r.FillTriangle(cx - 6, cy - 9, cx - 6, cy + 9, cx + 7, cy, icon);
            break;
        default:
            break;
        }
    }

    // 中心那格：喇叭在上、数字在下，两者各自水平居中。
    //
    // 喇叭的号角要"左窄右宽"：三角形的尖端贴在箱体上、竖边在外侧。
    // 反过来画（尖端朝右）出来就是个播放箭头，认不出是喇叭。
    //
    // 图标宽度固定成 12 像素（不随音量增减声波数量），否则居中的位置会跟着跳。
    const int kIconWidth = 12;
    const Color speaker = (volume == 0) ? Theme::kTextDisabled : Theme::kTextDim;
    const int icon_x = kStickHub.x + (kStickHub.w - kIconWidth) / 2;
    const int icon_y = kStickHub.y + 16;

    r.FillRect(icon_x, icon_y - 3, 4, 7, speaker);                          // 箱体
    r.FillTriangle(icon_x + 3, icon_y, icon_x + 8, icon_y - 7, icon_x + 8, icon_y + 7, speaker);
    if (volume == 0)
    {
        // 静音画个叉，压在号角右侧
        r.DrawLine(icon_x + 8, icon_y - 5, icon_x + 13, icon_y + 5, Theme::kHighlight);
        r.DrawLine(icon_x + 13, icon_y - 5, icon_x + 8, icon_y + 5, Theme::kHighlight);
    }
    else
    {
        r.DrawLine(icon_x + 11, icon_y - 4, icon_x + 11, icon_y + 4, speaker);   // 声波
    }

    char volume_text[16];
    std::snprintf(volume_text, sizeof(volume_text), "%d", volume);
    r.DrawText(volume_text, kStickHub.x + kStickHub.w / 2, kStickHub.y + 26,
               CRenderer::FS_SMALL, Theme::kTextDim, CRenderer::ALIGN_CENTER);

    // 逐秒进退这两个键没有数字说明，标一行免得被当成上一曲/下一曲。
    // 放在右键右侧而不是十字下方：下方紧挨着歌词，横向那块反而是空的。
    r.DrawText("±1 秒", kBtnSeekForward.x + kBtnSeekForward.w + 8,
               kStickRowMid + kStickCell / 2 - 10, CRenderer::FS_SMALL, Theme::kTextDisabled);
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
        { kBtnFaceB, HIT_FACE_B, "B", "退出" },
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

    r.FillRoundRect(kLeftX, m_layout.progress_y, m_layout.left_width, kProgressH,
                    kProgressH / 2, Theme::kPanelAlt);

    if (length > 0)
    {
        double ratio = Clamp01(static_cast<double>(preview) / length);
        int filled = static_cast<int>(m_layout.left_width * ratio);
        r.FillRoundRect(kLeftX, m_layout.progress_y, filled, kProgressH, kProgressH / 2,
                        dragging ? Theme::kHighlight : Theme::kAccent);
        // 拖动手柄
        r.FillRoundRect(kLeftX + filled - 6, m_layout.progress_y - 5, 12, 16, 6, Theme::kText);
    }

    CPlayTime pos_time{ preview };
    CPlayTime len_time{ length };
    r.DrawText(pos_time.toString(false), kLeftX, m_layout.progress_y + 16, CRenderer::FS_SMALL,
               Theme::kTextDim);
    r.DrawText(length > 0 ? len_time.toString(false) : std::string("-:--"),
               kLeftX + m_layout.left_width, m_layout.progress_y + 16, CRenderer::FS_SMALL,
               Theme::kTextDim,
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
    // 关掉触摸就是沉浸模式：按钮点不动了，画着只是挡住封面和歌词，
    // 所以三个操作区和工具排全收起来，左栏的封面同时放大（见 ComputeLayout）。
    // 顶栏不收：那里有重新打开触摸的开关，收了就再也开不回来。
    const bool show_touch_ui = ctx.input->IsTouchEnabled();

    DrawCover(ctx);
    DrawSongInfo(ctx);
    if (show_touch_ui)
        DrawTransportButtons(ctx);
    DrawProgressBar(ctx);

    // 触摸判定用的是这一帧记下的矩形，下一帧才生效；首帧为空不会误命中
    m_lyric_rect = m_layout.right;

    if (m_view == VIEW_LYRIC)
    {
        DrawLyricBackground(ctx, m_layout.right.x, m_layout.right.y,
                            m_layout.right.w, m_layout.right.h);
        DrawLyricView(ctx, m_layout.right.x, m_layout.right.y,
                      m_layout.right.w, m_layout.right.h);
    }
    else if (m_view == VIEW_COVER)
        DrawCoverView(ctx, m_layout.right.x, m_layout.right.y, m_layout.right.w,
                      m_layout.right.h);
    else
        DrawSpectrumView(ctx, m_layout.right.x, m_layout.right.y, m_layout.right.w,
                         m_layout.right.h);

    // 工具区画在视图之上，且不分视图：音量和下载在哪个视图下都得点得到
    if (show_touch_ui)
    {
        DrawLyricTools(ctx);
        DrawStickCross(ctx);
        if (FaceButtonsVisible(ctx))
            DrawFaceButtons(ctx);
    }

    DrawVolumeOverlay(ctx);
}
