#include "InputMap.h"

#include <switch.h>

#include <cmath>

namespace
{
    PadState g_pad;
    bool g_pad_inited = false;

    // 长按重复：先等 0.4 秒，之后每 0.08 秒触发一次
    const double kRepeatDelay = 0.40;
    const double kRepeatInterval = 0.08;

    // 摇杆死区。JOYSTICK_MAX 是 libnx 的满量程值
    const float kStickDeadZone = 0.25f;

    float NormalizeStick(int32_t raw)
    {
        float value = static_cast<float>(raw) / JOYSTICK_MAX;
        if (value > 1.0f) value = 1.0f;
        if (value < -1.0f) value = -1.0f;
        if (std::fabs(value) < kStickDeadZone)
            return 0.0f;
        // 把死区外的部分重新拉伸到 0~1，避免手感突变
        float sign = (value < 0.0f) ? -1.0f : 1.0f;
        return sign * (std::fabs(value) - kStickDeadZone) / (1.0f - kStickDeadZone);
    }

    uint64_t ButtonMask(CInputMap::Button button)
    {
        switch (button)
        {
        case CInputMap::BTN_A:        return HidNpadButton_A;
        case CInputMap::BTN_B:        return HidNpadButton_B;
        case CInputMap::BTN_X:        return HidNpadButton_X;
        case CInputMap::BTN_Y:        return HidNpadButton_Y;
        case CInputMap::BTN_L:        return HidNpadButton_L;
        case CInputMap::BTN_R:        return HidNpadButton_R;
        case CInputMap::BTN_ZL:       return HidNpadButton_ZL;
        case CInputMap::BTN_ZR:       return HidNpadButton_ZR;
        case CInputMap::BTN_PLUS:     return HidNpadButton_Plus;
        case CInputMap::BTN_MINUS:    return HidNpadButton_Minus;
        // 方向键与左摇杆方向合并，两种操作方式等价
        case CInputMap::BTN_UP:       return HidNpadButton_Up | HidNpadButton_StickLUp;
        case CInputMap::BTN_DOWN:     return HidNpadButton_Down | HidNpadButton_StickLDown;
        case CInputMap::BTN_LEFT:     return HidNpadButton_Left | HidNpadButton_StickLLeft;
        case CInputMap::BTN_RIGHT:    return HidNpadButton_Right | HidNpadButton_StickLRight;
        // 分开的版本：播放界面把方向键留给走带控制，音量交给摇杆
        case CInputMap::BTN_DPAD_UP:    return HidNpadButton_Up;
        case CInputMap::BTN_DPAD_DOWN:  return HidNpadButton_Down;
        case CInputMap::BTN_DPAD_LEFT:  return HidNpadButton_Left;
        case CInputMap::BTN_DPAD_RIGHT: return HidNpadButton_Right;
        case CInputMap::BTN_STICK_UP:    return HidNpadButton_StickLUp;
        case CInputMap::BTN_STICK_DOWN:  return HidNpadButton_StickLDown;
        case CInputMap::BTN_STICK_LEFT:  return HidNpadButton_StickLLeft;
        case CInputMap::BTN_STICK_RIGHT: return HidNpadButton_StickLRight;
        case CInputMap::BTN_STICK_L:  return HidNpadButton_StickL;
        case CInputMap::BTN_STICK_R:  return HidNpadButton_StickR;
        default:                      return 0;
        }
    }
}

void CInputMap::Init()
{
    if (g_pad_inited)
        return;
    // 同时接受掌机模式和最多 8 个手柄，任意一个都能操作
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&g_pad);
    hidInitializeTouchScreen();
    g_pad_inited = true;
}

void CInputMap::Update(double delta_seconds)
{
    m_should_exit = !appletMainLoop();

    padUpdate(&g_pad);
    uint64_t held = padGetButtons(&g_pad);
    uint64_t down = padGetButtonsDown(&g_pad);
    uint64_t up = padGetButtonsUp(&g_pad);

    for (int i = 0; i < BTN_COUNT; ++i)
    {
        uint64_t mask = ButtonMask(static_cast<Button>(i));
        m_held[i] = (held & mask) != 0;
        m_down[i] = (down & mask) != 0;
        m_up[i] = (up & mask) != 0;

        // 长按重复
        m_repeat[i] = m_down[i];
        if (m_held[i])
        {
            double before = m_hold_time[i];
            m_hold_time[i] += delta_seconds;
            if (before < kRepeatDelay && m_hold_time[i] >= kRepeatDelay)
            {
                m_repeat[i] = true;
            }
            else if (m_hold_time[i] >= kRepeatDelay)
            {
                double since_delay_before = before - kRepeatDelay;
                double since_delay_now = m_hold_time[i] - kRepeatDelay;
                if (static_cast<int>(since_delay_now / kRepeatInterval)
                    > static_cast<int>(since_delay_before / kRepeatInterval))
                {
                    m_repeat[i] = true;
                }
            }
        }
        else
        {
            m_hold_time[i] = 0.0;
        }
    }

    HidAnalogStickState left = padGetStickPos(&g_pad, 0);
    HidAnalogStickState right = padGetStickPos(&g_pad, 1);
    m_left_x = NormalizeStick(left.x);
    m_left_y = NormalizeStick(left.y);
    m_right_x = NormalizeStick(right.x);
    m_right_y = NormalizeStick(right.y);

    // ---- 触摸 ----
    bool was_touching = m_touch.touching;
    HidTouchScreenState touch_state{};
    bool has_touch = hidGetTouchScreenStates(&touch_state, 1) && touch_state.count > 0;

    m_touch.pressed = false;
    m_touch.released = false;
    if (has_touch)
    {
        int prev_x = m_touch.x;
        int prev_y = m_touch.y;
        m_touch.x = static_cast<int>(touch_state.touches[0].x);
        m_touch.y = static_cast<int>(touch_state.touches[0].y);
        if (!was_touching)
        {
            m_touch.pressed = true;
            m_touch.start_x = m_touch.x;
            m_touch.start_y = m_touch.y;
            prev_x = m_touch.x;                 // 刚按下这帧没有位移
            prev_y = m_touch.y;
        }
        m_touch.delta_x = m_touch.x - m_touch.start_x;
        m_touch.delta_y = m_touch.y - m_touch.start_y;
        m_touch.step_x = m_touch.x - prev_x;
        m_touch.step_y = m_touch.y - prev_y;
        m_touch.touching = true;
    }
    else
    {
        if (was_touching)
            m_touch.released = true;
        m_touch.touching = false;
        // 注意：抬手这一帧不能清 delta_x/y，界面要靠它判断这是点击还是拖动结束
        m_touch.step_x = 0;
        m_touch.step_y = 0;
    }

    // 空闲计时用：摇杆的判断走归一化后的值，死区内的漂移不算操作
    m_has_any_input = (held != 0) || (down != 0) || (up != 0)
                      || m_touch.touching || m_touch.released
                      || m_left_x != 0.0f || m_left_y != 0.0f
                      || m_right_x != 0.0f || m_right_y != 0.0f;
}
