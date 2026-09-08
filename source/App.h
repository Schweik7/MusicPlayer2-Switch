#pragma once
#include "Player.h"
#include "ScreenDimmer.h"
#include "net/Updater.h"
#include "input/InputMap.h"
#include "ui/BrowserScreen.h"
#include "ui/DownloadScreen.h"
#include "ui/PlayerScreen.h"
#include "ui/PlaylistScreen.h"
#include "ui/SettingsScreen.h"
#include "ui/Renderer.h"
#include "ui/Screen.h"

#include <memory>

// 应用外壳：持有各子系统，跑主循环，画公共的顶栏/底栏/提示条。
class CApp
{
public:
    // self_path 来自 main 的 argv[0]，自动更新要靠它替换自身
    void SetSelfPath(const std::string& path) { m_self_path = path; }
    bool Init();
    void Run();
    void Uninit();

    const std::string& GetLastError() const { return m_last_error; }

private:
    void SwitchScreen(ScreenId id);
    // 顶栏上的触摸按钮在任何界面下都有效，所以放在 App 这一层处理
    void HandleHeaderTouch();
    void DrawHeader();
    void DrawFooter();
    void DrawToast(double delta_seconds);
    // 省电模式下盖一层暗色并给出提示，让用户知道不是死机了
    void DrawDimOverlay();
    // 启动时恢复上次的播放列表；没有就扫描默认音乐目录
    void RestoreLastSession();
    // 下载完成后让新歌词/新封面立刻生效
    void HandleDownloadResult();

    CRenderer m_renderer;
    CPlayer m_player;
    CInputMap m_input;
    CScreenDimmer m_dimmer;
    CUpdater m_updater;

    CPlayerScreen m_player_screen;
    CPlaylistScreen m_playlist_screen;
    CBrowserScreen m_browser_screen;
    CDownloadScreen m_download_screen;
    CSettingsScreen m_settings_screen;
    CScreen* m_screens[SCREEN_COUNT]{};
    ScreenId m_current{ SCREEN_PLAYER };

    ScreenContext m_ctx;
    std::string m_last_error;
    bool m_running{};

    // 顶栏的两个触摸按钮。矩形在 DrawHeader 里按文字宽度算出，
    // 供下一帧的触摸判定使用——首帧是空矩形，不会误命中。
    Rect m_repeat_button;
    Rect m_touch_button;
    Rect m_settings_button;

    std::string m_self_path;
    // 上一帧观察到的下载状态，用于识别"刚刚完成"这个瞬间
    int m_last_download_state{};
};
