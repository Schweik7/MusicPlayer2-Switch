#include "SettingsScreen.h"
#include "Renderer.h"
#include "../Player.h"
#include "../core/FileUtil.h"
#include "../Version.h"
#include "../input/InputMap.h"
#include "../net/Updater.h"
#include "../App.h"

#include <algorithm>
#include <cstdio>

namespace
{
    const int kRowHeight = 64;
    const int kListX = Theme::kPadding * 2;
    const int kListY = Theme::kHeaderHeight + Theme::kPadding;
    const int kListW = Theme::kScreenWidth - kListX * 2;

    // 两栏排布。
    //
    // 原来是一列铺到底：九行 × 64 像素从 96 排到 672，而底栏从 656 开始——
    // 画在列表下方的更新状态和下载进度条整个落到屏幕外面，永远看不见。
    // 屏幕本来就宽（1280），一行只放一个"标签 + 值"也浪费，改成两栏正好
    // 把高度砍掉一半，下面空出两百多像素给状态信息。
    const int kColumnGap = 32;
    const int kColumnW = (kListW - kColumnGap) / 2;

    // 每栏放几行：按项数平分成两栏，而不是写死一个数。
    // 写死的话每加一个设置项都得回来改，忘了就会多出第三栏跑到屏幕外面去。
    int RowsPerColumn(int count)
    {
        return (count + 1) / 2;
    }

    // 第 index 项所在的格子
    Rect RowRect(int index, int count)
    {
        const int per_column = RowsPerColumn(count);
        const int column = index / per_column;
        const int row = index % per_column;
        return Rect{ kListX + column * (kColumnW + kColumnGap), kListY + row * kRowHeight,
                     kColumnW, kRowHeight };
    }

    // 状态信息区的顶端：跟在较长那一栏的下面
    int StatusTop(int count)
    {
        return kListY + RowsPerColumn(count) * kRowHeight + 16;
    }

    // 自动变暗的可选档位（秒）。0 表示关闭
    const int kDimOptions[] = { 0, 30, 60, 120, 300 };
    const int kDimOptionCount = static_cast<int>(sizeof(kDimOptions) / sizeof(kDimOptions[0]));

    std::string DimText(int seconds)
    {
        if (seconds <= 0)
            return "关闭";
        if (seconds < 60)
            return std::to_string(seconds) + " 秒";
        if (seconds % 60 == 0)
            return std::to_string(seconds / 60) + " 分钟";
        return std::to_string(seconds) + " 秒";
    }

    std::string BackgroundText(CConfig::LyricBackground mode)
    {
        switch (mode)
        {
        case CConfig::LB_COVER: return "当前曲目封面";
        case CConfig::LB_FILE:  return "自定义图片";
        default:                return "关闭";
        }
    }

    std::string FormatBytes(uint64_t bytes)
    {
        char buff[32];
        if (bytes >= 1024 * 1024)
            std::snprintf(buff, sizeof(buff), "%.1f MB", bytes / (1024.0 * 1024.0));
        else
            std::snprintf(buff, sizeof(buff), "%.0f KB", bytes / 1024.0);
        return buff;
    }
}

void CSettingsScreen::OnEnter(ScreenContext& ctx)
{
    m_show_about = false;
    m_selected = 0;
    BuildRows(ctx);
}

const char* CSettingsScreen::GetButtonHints() const
{
    if (m_show_about)
        return "B 返回设置";
    return "A 修改/执行|B 返回|方向键 移动|左右 切换栏";
}

void CSettingsScreen::GoBack(ScreenContext& ctx)
{
    // 关于页是设置的子页，先退回列表
    if (m_show_about)
        m_show_about = false;
    else
        ctx.next_screen = SCREEN_PLAYER;
}

void CSettingsScreen::BuildRows(ScreenContext& ctx)
{
    CPlayer& player = *ctx.player;
    CConfig& config = player.GetConfig();

    m_rows.assign(ITEM_COUNT, Row());

    m_rows[ITEM_TOUCH].label = "触摸操作";
    m_rows[ITEM_TOUCH].value = ctx.input->IsTouchEnabled() ? "开启" : "关闭";
    m_rows[ITEM_TOUCH].actionable = true;

    m_rows[ITEM_DIM].label = "空闲自动变暗";
    m_rows[ITEM_DIM].value = DimText(config.GetDimTimeout());
    m_rows[ITEM_DIM].actionable = true;

    m_rows[ITEM_TRANSLATION].label = "显示歌词翻译";
    m_rows[ITEM_TRANSLATION].value = config.GetShowTranslation() ? "开启" : "关闭";
    m_rows[ITEM_TRANSLATION].actionable = true;

    m_rows[ITEM_LYRIC_LAYOUT].label = "双语歌词排版";
    m_rows[ITEM_LYRIC_LAYOUT].value = config.GetLyricTwoColumn() ? "双栏（左原文 右译文）"
                                                                 : "单栏（译文在下方）";
    m_rows[ITEM_LYRIC_LAYOUT].actionable = true;

    m_rows[ITEM_LYRIC_SYNC].label = "拖歌词跟随进度";
    m_rows[ITEM_LYRIC_SYNC].value = config.GetLyricSeekSync() ? "开启" : "关闭（仅翻看）";
    m_rows[ITEM_LYRIC_SYNC].actionable = true;

    m_rows[ITEM_LYRIC_BACKGROUND].label = "歌词区背景";
    m_rows[ITEM_LYRIC_BACKGROUND].value = BackgroundText(config.GetLyricBackground());
    m_rows[ITEM_LYRIC_BACKGROUND].actionable = true;

    m_rows[ITEM_IMMERSIVE].label = "沉浸模式";
    m_rows[ITEM_IMMERSIVE].value = config.GetImmersive() ? "开启（收起屏上按钮）" : "关闭";
    m_rows[ITEM_IMMERSIVE].actionable = true;

    m_rows[ITEM_EMBED].label = "下载后写入歌曲文件";
    m_rows[ITEM_EMBED].value = config.GetEmbedDownloads() ? "开启" : "关闭（只存旁边）";
    m_rows[ITEM_EMBED].actionable = true;

    m_rows[ITEM_BROWSE].label = "浏览 SD 卡";
    m_rows[ITEM_BROWSE].value = "选择要播放的目录";
    m_rows[ITEM_BROWSE].actionable = true;

    m_rows[ITEM_MUSIC_DIR].label = "默认音乐目录";
    m_rows[ITEM_MUSIC_DIR].value = config.GetMusicDir();
    m_rows[ITEM_MUSIC_DIR].actionable = false;

    m_rows[ITEM_NETWORK].label = "网络";
    if (!player.IsNetworkReady())
        m_rows[ITEM_NETWORK].value = "不可用";
    else
        m_rows[ITEM_NETWORK].value = player.IsCertVerified() ? "已连接 · 证书已验证"
                                                             : "已连接 · 证书未验证";
    m_rows[ITEM_NETWORK].actionable = false;

    m_rows[ITEM_UPDATE].label = "检查更新";
    m_rows[ITEM_UPDATE].value = "当前 " MP2_SWITCH_VERSION;
    m_rows[ITEM_UPDATE].actionable = true;

    m_rows[ITEM_ABOUT].label = "关于";
    m_rows[ITEM_ABOUT].actionable = true;
}

void CSettingsScreen::Activate(ScreenContext& ctx, int index)
{
    CPlayer& player = *ctx.player;
    CConfig& config = player.GetConfig();

    switch (index)
    {
    case ITEM_TOUCH:
    {
        bool enabled = !ctx.input->IsTouchEnabled();
        ctx.input->SetTouchEnabled(enabled);
        config.SetTouchEnabled(enabled);
        ctx.ShowToast(enabled ? "已启用触摸操作" : "已禁用触摸操作");
        break;
    }
    case ITEM_DIM:
    {
        // 在几个档位之间循环
        int current = config.GetDimTimeout();
        int next = 0;
        for (int i = 0; i < kDimOptionCount; ++i)
        {
            if (kDimOptions[i] == current)
            {
                next = kDimOptions[(i + 1) % kDimOptionCount];
                break;
            }
        }
        config.SetDimTimeout(next);
        // 变更立刻生效：CApp 每帧都从这里读不现实，所以直接改上下文里的提示
        ctx.ShowToast("自动变暗：" + DimText(next));
        break;
    }
    case ITEM_TRANSLATION:
        config.SetShowTranslation(!config.GetShowTranslation());
        break;
    case ITEM_LYRIC_LAYOUT:
    {
        bool two_column = !config.GetLyricTwoColumn();
        config.SetLyricTwoColumn(two_column);
        ctx.ShowToast(two_column ? "歌词改为双栏显示" : "歌词改为单栏显示");
        break;
    }
    case ITEM_LYRIC_SYNC:
    {
        const bool sync = !config.GetLyricSeekSync();
        config.SetLyricSeekSync(sync);
        ctx.ShowToast(sync ? "拖动歌词将同时改变播放进度"
                           : "拖动歌词只是翻看，松手后自动归位");
        break;
    }
    case ITEM_LYRIC_BACKGROUND:
    {
        // 在几档之间循环
        int next = (config.GetLyricBackground() + 1) % CConfig::LB_COUNT;
        config.SetLyricBackground(static_cast<CConfig::LyricBackground>(next));
        if (next == CConfig::LB_FILE)
        {
            // 这一档要用户自己放图，找不到就说清楚放哪儿
            const std::string path = FileUtil::Combine(CPlayer::GetDataDir(), "background.jpg");
            ctx.ShowToast(FileUtil::Exists(path) || FileUtil::Exists(
                              FileUtil::Combine(CPlayer::GetDataDir(), "background.png"))
                              ? "歌词区背景：自定义图片"
                              : "把 background.jpg 放进 " + CPlayer::GetDataDir());
        }
        else
        {
            ctx.ShowToast("歌词区背景：" + BackgroundText(
                              static_cast<CConfig::LyricBackground>(next)));
        }
        break;
    }
    case ITEM_IMMERSIVE:
    {
        const bool immersive = !config.GetImmersive();
        config.SetImmersive(immersive);
        ctx.ShowToast(immersive ? "沉浸模式：收起屏上按钮，只留封面和歌词"
                                : "沉浸模式：关闭");
        break;
    }
    case ITEM_EMBED:
    {
        const bool embed = !config.GetEmbedDownloads();
        config.SetEmbedDownloads(embed);
        ctx.ShowToast(embed ? "下载的歌词封面将写进 MP3 / FLAC 文件"
                            : "下载的歌词封面只保存在歌曲旁边");
        break;
    }
    case ITEM_UPDATE:
    {
        if (m_updater == nullptr)
            break;
        if (!player.IsNetworkReady())
        {
            ctx.ShowToast("网络不可用");
            break;
        }
        CUpdater::Status status = m_updater->Poll();
        if (status.state == CUpdater::ST_UPDATE_AVAILABLE)
        {
            if (!m_updater->StartInstall())
                ctx.ShowToast("更新正在进行中");
        }
        else if (!m_updater->IsBusy())
        {
            m_updater->Reset();
            m_updater->StartCheck();
        }
        break;
    }
    case ITEM_BROWSE:
        ctx.next_screen = SCREEN_BROWSER;
        break;
    case ITEM_ABOUT:
        m_show_about = true;
        break;
    default:
        break;
    }

    BuildRows(ctx);
}

void CSettingsScreen::Update(ScreenContext& ctx, double delta_seconds)
{
    (void)delta_seconds;
    CInputMap& input = *ctx.input;

    if (input.IsDown(CInputMap::BTN_B))
    {
        GoBack(ctx);
        return;
    }
    if (m_show_about)
        return;

    // 每帧重建行内容，更新状态（下载进度等）才能实时反映出来
    BuildRows(ctx);

    const int count = static_cast<int>(m_rows.size());

    // 上下在本栏内循环，左右换栏。这样光标的移动和眼睛看到的排布是一致的，
    // 一路按"下"从左栏底部窜到右栏顶部反而会让人跟丢。
    const int per_column = RowsPerColumn(count);
    const int column_top = (m_selected / per_column) * per_column;
    const int column_size = std::min(per_column, count - column_top);

    if (input.IsRepeat(CInputMap::BTN_DOWN))
        m_selected = column_top + (m_selected - column_top + 1) % column_size;
    if (input.IsRepeat(CInputMap::BTN_UP))
        m_selected = column_top + (m_selected - column_top - 1 + column_size) % column_size;
    if (input.IsRepeat(CInputMap::BTN_RIGHT) && m_selected + per_column < count)
        m_selected += per_column;
    if (input.IsRepeat(CInputMap::BTN_LEFT) && m_selected - per_column >= 0)
        m_selected -= per_column;

    if (input.IsDown(CInputMap::BTN_A) && m_rows[m_selected].actionable)
        Activate(ctx, m_selected);

    // 触摸：点某一格
    const CInputMap::TouchState& touch = input.GetTouch();
    if (touch.released && !touch.IsDrag())
    {
        for (int i = 0; i < count; ++i)
        {
            if (!RowRect(i, count).Contains(touch.x, touch.y))
                continue;
            m_selected = i;
            if (m_rows[i].actionable)
                Activate(ctx, i);
            break;
        }
    }
}

void CSettingsScreen::DrawUpdateStatus(ScreenContext& ctx, int x, int y, int width)
{
    if (m_updater == nullptr)
        return;
    CRenderer& r = *ctx.renderer;
    CUpdater::Status status = m_updater->Poll();
    if (status.state == CUpdater::ST_IDLE)
        return;

    Color color = Theme::kTextDim;
    if (status.state == CUpdater::ST_FAILED)
        color = Theme::kHighlight;
    else if (status.state == CUpdater::ST_INSTALLED
             || status.state == CUpdater::ST_UPDATE_AVAILABLE)
        color = Theme::kAccent;

    r.DrawTextEllipsis(status.message, x, y, width, CRenderer::FS_SMALL, color);

    if (status.state == CUpdater::ST_DOWNLOADING && status.total > 0)
    {
        double ratio = static_cast<double>(status.downloaded) / status.total;
        ratio = std::max(0.0, std::min(1.0, ratio));
        const int bar_y = y + 26;
        r.FillRoundRect(x, bar_y, width, 6, 3, Theme::kPanelAlt);
        r.FillRoundRect(x, bar_y, static_cast<int>(width * ratio), 6, 3, Theme::kAccent);

        std::string text = FormatBytes(status.downloaded) + " / " + FormatBytes(status.total);
        r.DrawText(text, x + width, bar_y + 12, CRenderer::FS_SMALL, Theme::kTextDisabled,
                   CRenderer::ALIGN_RIGHT);
    }
    else if (status.state == CUpdater::ST_UPDATE_AVAILABLE)
    {
        if (status.asset_verified)
        {
            r.DrawText("按 A 下载并安装", x, y + 26, CRenderer::FS_SMALL, Theme::kTextDim);
        }
        else
        {
            // 明文 http 的下载地址验不了服务器身份，装的是一个来路无法确认的
            // 可执行文件。这话必须说在按 A 之前。
            r.DrawText("按 A 下载并安装 · 来自备用源，无法验证服务器身份",
                       x, y + 26, CRenderer::FS_SMALL, Theme::kHighlight);
        }
    }
}

void CSettingsScreen::DrawAbout(ScreenContext& ctx)
{
    CRenderer& r = *ctx.renderer;
    int y = kListY + 20;

    r.DrawText("MusicPlayer2 for Nintendo Switch", kListX, y, CRenderer::FS_LARGE, Theme::kText);
    y += 56;

    struct Line { const char* label; std::string value; };
    const Line lines[] = {
        { "版本",     MP2_SWITCH_VERSION },
        { "构建时间", std::string(__DATE__) + " " + __TIME__ },
        { "项目地址", MP2_SWITCH_REPO_URL },
        { "上游项目", "https://github.com/zhongyang219/MusicPlayer2" },
        { "程序位置", m_updater != nullptr ? m_updater->GetSelfPath() : std::string("-") },
    };

    for (const Line& line : lines)
    {
        r.DrawText(line.label, kListX, y, CRenderer::FS_NORMAL, Theme::kTextDim);
        r.DrawTextEllipsis(line.value, kListX + 180, y, kListW - 180, CRenderer::FS_NORMAL,
                           Theme::kText);
        y += 42;
    }

    // 启动耗时。"启动要十秒"这种事光靠猜没用，把每一段的耗时摆出来才好定位。
    if (m_app != nullptr && !m_app->GetBootTimings().empty())
    {
        y += 12;
        r.DrawText("启动耗时", kListX, y, CRenderer::FS_NORMAL, Theme::kTextDim);
        int bx = kListX + 180;
        for (const CApp::BootStageTime& stage : m_app->GetBootTimings())
        {
            char text[64];
            std::snprintf(text, sizeof(text), "%s %u ms", stage.name, stage.ms);
            int w = 0, h = 0;
            r.MeasureText(text, CRenderer::FS_SMALL, w, h);
            if (bx + w > kListX + kListW)        // 一行放不下就换行
            {
                bx = kListX + 180;
                y += 28;
            }
            r.DrawText(text, bx, y + 4, CRenderer::FS_SMALL, Theme::kText);
            bx += w + 24;
        }
        y += 42;
    }

    y += 20;
    r.DrawText("本移植版重写了界面与音频层：桌面版基于 MFC，无法交叉编译到 Switch。",
               kListX, y, CRenderer::FS_SMALL, Theme::kTextDisabled);
    y += 30;
    r.DrawText("核心的播放列表、歌词解析、在线下载逻辑与桌面版保持一致。",
               kListX, y, CRenderer::FS_SMALL, Theme::kTextDisabled);
}

void CSettingsScreen::Draw(ScreenContext& ctx)
{
    if (m_show_about)
    {
        DrawAbout(ctx);
        return;
    }

    CRenderer& r = *ctx.renderer;

    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        const Row& row = m_rows[i];
        const Rect cell = RowRect(static_cast<int>(i), static_cast<int>(m_rows.size()));
        const bool selected = (static_cast<int>(i) == m_selected);

        if (selected)
            r.FillRoundRect(cell.x, cell.y + 2, cell.w, cell.h - 4, 6, Theme::kSelection);

        Color label_color = row.actionable ? Theme::kText : Theme::kTextDim;
        r.DrawText(row.label, cell.x + 16, cell.y + 18, CRenderer::FS_NORMAL, label_color);

        if (!row.value.empty())
        {
            // 值靠右，最多占一半格宽，剩下的留给标签
            r.DrawTextEllipsis(row.value, cell.x + cell.w - 16, cell.y + 20, cell.w / 2,
                               CRenderer::FS_SMALL, Theme::kTextDim, CRenderer::ALIGN_RIGHT);
        }
    }

    // 更新状态显示在列表下方。两栏之后这里终于有地方了。
    DrawUpdateStatus(ctx, kListX + 16, StatusTop(static_cast<int>(m_rows.size())), kListW - 32);
}
