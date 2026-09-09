#pragma once

// 电量与充电状态。
//
// 走 libnx 的 psm 服务。这个服务每次查询都是一次 IPC，60fps 下每帧问一次纯属浪费，
// 所以内部按固定间隔刷新，中间的调用直接返回缓存——顶栏那个图标本来也不需要更快。
namespace SystemPower
{
    struct BatteryState
    {
        int  percent{ -1 };     // 0~100；读不到时为 -1
        bool charging{};
        bool valid{};
    };

    void Init();
    void Uninit();

    // 带缓存，可以每帧调用
    BatteryState Get();
}
