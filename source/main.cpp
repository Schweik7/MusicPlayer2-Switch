#include "App.h"

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
    (void)argc;
    (void)argv;

    CApp app;
    if (!app.Init())
    {
        std::string error = app.GetLastError();
        app.Uninit();
        ShowFatalError(error.empty() ? "未知错误" : error);
        return 1;
    }

    app.Run();
    app.Uninit();
    return 0;
}
