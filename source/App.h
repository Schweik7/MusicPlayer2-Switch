#pragma once
#include "Player.h"
#include "input/InputMap.h"
#include "ui/BrowserScreen.h"
#include "ui/DownloadScreen.h"
#include "ui/PlayerScreen.h"
#include "ui/PlaylistScreen.h"
#include "ui/Renderer.h"
#include "ui/Screen.h"

#include <memory>

// 应用外壳：持有各子系统，跑主循环，画公共的顶栏/底栏/提示条。
class CApp
{
public:
    bool Init();
    void Run();
    void Uninit();

    const std::string& GetLastError() const { return m_last_error; }

private:
    void SwitchScreen(ScreenId id);
    void DrawHeader();
    void DrawFooter();
    void DrawToast(double delta_seconds);
    // 启动时恢复上次的播放列表；没有就扫描默认音乐目录
    void RestoreLastSession();
    // 下载完成后让新歌词/新封面立刻生效
    void HandleDownloadResult();

    CRenderer m_renderer;
    CPlayer m_player;
    CInputMap m_input;

    CPlayerScreen m_player_screen;
    CPlaylistScreen m_playlist_screen;
    CBrowserScreen m_browser_screen;
    CDownloadScreen m_download_screen;
    CScreen* m_screens[SCREEN_COUNT]{};
    ScreenId m_current{ SCREEN_PLAYER };

    ScreenContext m_ctx;
    std::string m_last_error;
    bool m_running{};
    // 上一帧观察到的下载状态，用于识别"刚刚完成"这个瞬间
    int m_last_download_state{};
};
