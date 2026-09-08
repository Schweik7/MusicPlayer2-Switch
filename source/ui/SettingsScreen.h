#pragma once
#include "Screen.h"

#include <string>
#include <vector>

class CUpdater;

// 设置界面：偏好开关、关于、检查更新。
class CSettingsScreen : public CScreen
{
public:
    // updater 的生命周期由 CApp 持有
    void SetUpdater(CUpdater* updater) { m_updater = updater; }

    void OnEnter(ScreenContext& ctx) override;
    void Update(ScreenContext& ctx, double delta_seconds) override;
    void Draw(ScreenContext& ctx) override;

    const char* GetTitle() const override { return m_show_about ? "关于" : "设置"; }
    const char* GetButtonHints() const override;

private:
    enum ItemId
    {
        ITEM_TOUCH = 0,
        ITEM_DIM,
        ITEM_TRANSLATION,
        ITEM_LYRIC_LAYOUT,
        ITEM_BROWSE,
        ITEM_MUSIC_DIR,
        ITEM_NETWORK,
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

    CUpdater* m_updater{};
    std::vector<Row> m_rows;
    int m_selected{};
    bool m_show_about{};
};
