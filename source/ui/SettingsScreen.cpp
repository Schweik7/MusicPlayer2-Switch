#include "SettingsScreen.h"
#include "Renderer.h"
#include "../Player.h"
#include "../Version.h"
#include "../input/InputMap.h"
#include "../net/Updater.h"

#include <algorithm>
#include <cstdio>

namespace
{
    const int kRowHeight = 64;
    const int kListX = Theme::kPadding * 2;
    const int kListY = Theme::kHeaderHeight + Theme::kPadding;
    const int kListW = Theme::kScreenWidth - kListX * 2;

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
    return "A 修改/执行   B 返回   方向键 移动";
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
        if (m_show_about)
            m_show_about = false;
        else
            ctx.next_screen = SCREEN_PLAYER;
        return;
    }
    if (m_show_about)
        return;

    // 每帧重建行内容，更新状态（下载进度等）才能实时反映出来
    BuildRows(ctx);

    const int count = static_cast<int>(m_rows.size());
    if (input.IsRepeat(CInputMap::BTN_DOWN))
        m_selected = (m_selected + 1) % count;
    if (input.IsRepeat(CInputMap::BTN_UP))
        m_selected = (m_selected - 1 + count) % count;

    if (input.IsDown(CInputMap::BTN_A) && m_rows[m_selected].actionable)
        Activate(ctx, m_selected);

    // 触摸：点某一行
    const CInputMap::TouchState& touch = input.GetTouch();
    if (touch.released && !touch.IsDrag()
        && touch.y >= kListY && touch.y < kListY + count * kRowHeight)
    {
        int row = (touch.y - kListY) / kRowHeight;
        if (row >= 0 && row < count)
        {
            m_selected = row;
            if (m_rows[row].actionable)
                Activate(ctx, row);
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
        r.DrawText("按 A 下载并安装", x, y + 26, CRenderer::FS_SMALL, Theme::kTextDim);
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
        int y = kListY + static_cast<int>(i) * kRowHeight;
        bool selected = (static_cast<int>(i) == m_selected);

        if (selected)
            r.FillRoundRect(kListX, y + 2, kListW, kRowHeight - 4, 6, Theme::kSelection);

        Color label_color = row.actionable ? Theme::kText : Theme::kTextDim;
        r.DrawText(row.label, kListX + 16, y + 18, CRenderer::FS_NORMAL, label_color);

        if (!row.value.empty())
        {
            r.DrawTextEllipsis(row.value, kListX + kListW - 16, y + 20, kListW / 2,
                               CRenderer::FS_SMALL, Theme::kTextDim, CRenderer::ALIGN_RIGHT);
        }
    }

    // 更新状态显示在列表下方，留出进度条的空间
    DrawUpdateStatus(ctx, kListX + 16, kListY + static_cast<int>(m_rows.size()) * kRowHeight + 16,
                     kListW - 32);
}
