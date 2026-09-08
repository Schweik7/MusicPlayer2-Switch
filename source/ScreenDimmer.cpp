#include "ScreenDimmer.h"

#include <switch.h>

#include <algorithm>

namespace
{
    // 调暗后的亮度。不用 0：那样屏幕全黑，看不出程序还在跑，
    // 用户会以为死机了。留一点点亮度，仍然能看清正在播放什么。
    const float kDimBrightness = 0.05f;
}

void CScreenDimmer::Init()
{
    if (m_inited)
        return;
    if (R_FAILED(lblInitialize()))
        return;
    m_inited = true;

    // 记下用户原本的亮度和自动亮度开关，退出时要原样还回去
    float brightness = 1.0f;
    if (R_SUCCEEDED(lblGetCurrentBrightnessSetting(&brightness)))
    {
        m_saved_brightness = brightness;
        m_have_saved = true;
    }
    bool auto_brightness = false;
    if (R_SUCCEEDED(lblIsAutoBrightnessControlEnabled(&auto_brightness)))
        m_saved_auto_brightness = auto_brightness;
}

void CScreenDimmer::Uninit()
{
    if (!m_inited)
        return;
    // 即使程序异常结束也要尽量把亮度还回去，否则用户下次开机会一脸问号
    Restore();
    lblExit();
    m_inited = false;
}

void CScreenDimmer::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled)
    {
        Restore();
        m_idle = 0.0;
    }
}

void CScreenDimmer::Dim()
{
    if (!m_inited || m_dimmed)
        return;
    // 自动亮度开着的话会把我们设的值顶掉，调暗期间先关掉
    if (m_saved_auto_brightness)
        lblDisableAutoBrightnessControl();
    lblSetCurrentBrightnessSetting(kDimBrightness);
    m_dimmed = true;
}

void CScreenDimmer::Restore()
{
    if (!m_inited || !m_dimmed)
        return;
    if (m_have_saved)
        lblSetCurrentBrightnessSetting(m_saved_brightness);
    if (m_saved_auto_brightness)
        lblEnableAutoBrightnessControl();
    m_dimmed = false;
}

void CScreenDimmer::Update(double delta_seconds, bool any_input, bool allow_dim)
{
    if (!m_inited)
        return;

    if (any_input || !m_enabled || !allow_dim)
    {
        Restore();
        m_idle = 0.0;
        return;
    }

    m_idle += delta_seconds;
    if (m_idle >= m_timeout)
        Dim();
}

double CScreenDimmer::GetRemainingSeconds() const
{
    if (!m_inited || !m_enabled || m_dimmed)
        return 0.0;
    return std::max(0.0, m_timeout - m_idle);
}
