#pragma once
#include "Player.h"
#include "ScreenDimmer.h"
#include "core/LibraryScanner.h"
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
#include <vector>

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
    void ToggleTouchEnabled();
    void DrawHeader();
    void DrawFooter();
    void DrawToast(double delta_seconds);
    // 省电模式下盖一层暗色并给出提示，让用户知道不是死机了
    void DrawDimOverlay();
    // 启动时恢复上次的播放列表；没有就扫描默认音乐目录
    void RestoreLastSession();
    // 下载完成后让新歌词/新封面立刻生效
    void HandleDownloadResult();
    // 后台扫描完成后把结果接进播放列表
    void HandleScanResult();

    // 启动过程中每完成一个阶段记一笔耗时，并顺手把这句话画到屏幕上。
    // 记录是为了让"启动慢"这件事有据可查——各阶段耗时会显示在设置 → 关于里；
    // 绘制是为了别让用户对着黑屏干等。
    void BootStage(const char* name);

    CRenderer m_renderer;
    CPlayer m_player;
    CInputMap m_input;
    CScreenDimmer m_dimmer;
    CLibraryScanner m_scanner;
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

    // 顶栏的触摸按钮。矩形在 DrawHeader 里按文字宽度算出，
    // 供下一帧的触摸判定使用——首帧是空矩形，不会误命中。
    //
    // 顶栏放这些按钮的原则：凡是只能靠某个手柄键触发的功能，都要在这里有个入口，
    // 否则纯触摸操作的用户会被卡住（返回就是这么漏掉的）。
    Rect m_back_button;
    Rect m_repeat_button;
    Rect m_touch_button;
    Rect m_settings_button;
    Rect m_playlist_button;
    Rect m_volume_down_button;
    Rect m_volume_up_button;

    std::string m_self_path;
    // 上一帧观察到的下载状态，用于识别"刚刚完成"这个瞬间
    int m_last_download_state{};
    uint32_t m_scan_start_ticks{};

public:
    struct BootStageTime
    {
        const char* name;
        uint32_t ms;
    };
    const std::vector<BootStageTime>& GetBootTimings() const { return m_boot_timings; }

private:
    std::vector<BootStageTime> m_boot_timings;
    uint32_t m_boot_start_ticks{};
    uint32_t m_boot_last_ticks{};
};
