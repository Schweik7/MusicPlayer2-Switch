#pragma once

// libnx 的 socketInitializeDefault() 不是引用计数的：第二次调用会直接失败。
// 应用里有两处需要 socket —— 下载器（curl）和 nxlink 的 stdout 重定向 ——
// 谁先谁后取决于是否通过 nxlink 启动，所以这里加一层引用计数把它包起来。
namespace SocketGuard
{
    // 首次调用真正执行 socketInitializeDefault()，之后只增加引用计数。
    // 返回 false 表示底层初始化失败，此时引用计数不变。
    bool Acquire();

    // 引用计数归零时才真正 socketExit()。
    void Release();

    bool IsInited();
}
