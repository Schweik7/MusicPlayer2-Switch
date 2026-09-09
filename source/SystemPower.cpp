#include "SystemPower.h"

#include <switch.h>

namespace
{
    bool g_inited = false;

    SystemPower::BatteryState g_cache;
    u64  g_next_poll_tick = 0;
    bool g_polled_once = false;

    // 刷新间隔。电量分辨率本来就是 1%，掉 1% 要好几分钟，
    // 5 秒已经远快于它的变化速度。
    constexpr u64 kPollIntervalNs = 5ULL * 1000000000ULL;

    void Poll()
    {
        SystemPower::BatteryState state;

        u32 percent = 0;
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percent)))
        {
            state.percent = static_cast<int>(percent > 100 ? 100 : percent);
            state.valid = true;
        }

        PsmChargerType charger = PsmChargerType_Unconnected;
        if (R_SUCCEEDED(psmGetChargerType(&charger)))
        {
            // NotSupported 表示插着一个协商不出可用供电模式的充电器，
            // 这种情况其实并没有在充电，不能画成充电中
            state.charging = (charger == PsmChargerType_EnoughPower
                              || charger == PsmChargerType_LowPower);
        }

        g_cache = state;
    }
}

namespace SystemPower
{

void Init()
{
    if (g_inited)
        return;
    g_inited = R_SUCCEEDED(psmInitialize());
    g_polled_once = false;
    g_next_poll_tick = 0;
}

void Uninit()
{
    if (!g_inited)
        return;
    psmExit();
    g_inited = false;
    g_cache = BatteryState{};
    g_polled_once = false;
}

BatteryState Get()
{
    if (!g_inited)
        return BatteryState{};

    const u64 now = armGetSystemTick();
    if (!g_polled_once || now >= g_next_poll_tick)
    {
        Poll();
        g_polled_once = true;
        g_next_poll_tick = now + armNsToTicks(kPollIntervalNs);
    }
    return g_cache;
}

}   // namespace SystemPower
