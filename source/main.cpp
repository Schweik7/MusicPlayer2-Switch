#include "App.h"
#include "Diagnostics.h"

#include <switch.h>

#include <cstdio>

// 初始化失败时没法用图形界面报错，退回到控制台把原因打出来，
// 否则用户只会看到程序一闪而过。
static void ShowFatalError(const std::string& message)
{
    consoleInit(nullptr);
    std::printf("\n MusicPlayer2 启动失败\n\n %s\n\n 按 + 退出。\n", message.c_str());
    consoleUpdate(nullptr);

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

int main(int argc, char* argv[])
{
    // hbmenu 启动时 argv[0] 是本程序 NRO 在 SD 卡上的路径，自动更新要靠它替换自身
    std::string self_path;
    if (argc > 0 && argv[0] != nullptr && argv[0][0] != 0)
        self_path = argv[0];

    // 诊断要在 App 之前起来：文件系统探测的结论不依赖图形和音频是否初始化成功，
    // 而恰恰是这类问题最需要在启动失败时也能看到输出。
    Diag::Begin();
    Diag::ProbeFileSystem();

    CApp app;
    app.SetSelfPath(self_path);
    if (!app.Init())
    {
        std::string error = app.GetLastError();
        Diag::Logf("App::Init 失败: %s", error.c_str());
        app.Uninit();
        Diag::End();
        ShowFatalError(error.empty() ? "未知错误" : error);
        return 1;
    }

    app.Run();
    app.Uninit();
    Diag::End();
    return 0;
}
