#pragma once
#include "Screen.h"
#include "../core/Lang.h"

#include <string>
#include <vector>

class CUpdater;
class CApp;

// 设置界面：偏好开关、关于、检查更新。
class CSettingsScreen : public CScreen
{
public:
    // updater 的生命周期由 CApp 持有
    void SetUpdater(CUpdater* updater) { m_updater = updater; }
    // 关于页要显示启动各阶段的耗时，那份数据在 CApp 手上
    void SetApp(const CApp* app) { m_app = app; }

    void OnEnter(ScreenContext& ctx) override;
    void Update(ScreenContext& ctx, double delta_seconds) override;
    void Draw(ScreenContext& ctx) override;

    const char* GetTitle() const override { return m_show_about ? T("关于") : T("设置"); }
    const char* GetButtonHints() const override;
    void GoBack(ScreenContext& ctx) override;

private:
    enum ItemId
    {
        // 触摸开关和双语排版不在这里出现：前者在顶栏有常驻按钮（另有 LS），
        // 后者在歌词区工具排上有 1C/2C 按钮（另有 R）。
        // 两条路都齐了就不必在设置里再放一份——那只会把列表撑长，
        // 把底部的下载进度挤出可视区。
        ITEM_LANGUAGE = 0,
        ITEM_DIM,
        ITEM_THEME,
        ITEM_TRANSLATION,
        ITEM_LYRIC_SYNC,
        ITEM_LYRIC_OFFSET,
        ITEM_LYRIC_BACKGROUND,
        ITEM_HIDE_HINTS,
        ITEM_EMBED,
        ITEM_BROWSE,
        ITEM_MUSIC_DIR,
        ITEM_NETWORK,
        ITEM_UPDATE_SOURCE,
        ITEM_UPDATE,
        ITEM_ABOUT,
        ITEM_COUNT
    };

    struct Row
    {
        std::string label;
        std::string value;      // 右侧显示的当前值；空表示这是个动作项
        bool actionable{};      // 能否用 A 触发
    };

    void BuildRows(ScreenContext& ctx);
    void Activate(ScreenContext& ctx, int index);
    void DrawAbout(ScreenContext& ctx);
    void DrawUpdateStatus(ScreenContext& ctx, int x, int y, int width);

    void EnsureSelectionVisible(int count);

    CUpdater* m_updater{};
    const CApp* m_app{};
    std::vector<Row> m_rows;
    int m_selected{};
    bool m_show_about{};

    // 列表滚动。以行为单位（两栏共用同一个行窗口），
    // 和播放列表、文件浏览用的是同一套 ListScroller。
    int m_scroll{};
    double m_scroll_smooth{};
    bool m_dragging{};
    double m_fling{};
};
