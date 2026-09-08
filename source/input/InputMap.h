#pragma once
#include <cstdint>

// 手柄与触摸输入。
//
// 直接用 libnx 的 pad API 而不是 SDL 的手柄子系统：SDL 只负责视频，
// 两边同时读 HID 会互相干扰，而且 libnx 这边能拿到触摸和握持模式的完整信息。
//
// 按键约定（沿用 Switch 系统 UI 的习惯）：
//   A          播放/暂停
//   B          返回上一层
//   X          切换歌词/频谱视图
//   Y          切换播放模式（顺序/随机/列表循环/单曲循环）
//   L / R      上一首 / 下一首
//   ZL / ZR    快退 5 秒 / 快进 5 秒
//   方向键      播放界面：← → 上/下一曲，↑ 播放/暂停，↓ 停止；列表界面：移动光标
//   左摇杆      列表界面与方向键等价；播放界面上下调音量
//   右摇杆左右  拖动进度
//   +          打开设置
//   -          打开/关闭播放列表
//
// 方向键和左摇杆在列表里等价（BTN_UP 等），但播放界面要把两者分开用，
// 所以另外提供只认方向键的 BTN_DPAD_* 和只认摇杆的 BTN_STICK_UP/DOWN。
class CInputMap
{
public:
    enum Button
    {
        BTN_A = 0,
        BTN_B,
        BTN_X,
        BTN_Y,
        BTN_L,
        BTN_R,
        BTN_ZL,
        BTN_ZR,
        BTN_PLUS,
        BTN_MINUS,
        BTN_UP,             // 方向键或左摇杆，列表导航用
        BTN_DOWN,
        BTN_LEFT,
        BTN_RIGHT,
        BTN_DPAD_UP,        // 只认方向键
        BTN_DPAD_DOWN,
        BTN_DPAD_LEFT,
        BTN_DPAD_RIGHT,
        BTN_STICK_UP,       // 只认左摇杆
        BTN_STICK_DOWN,
        BTN_STICK_L,
        BTN_COUNT
    };

    struct TouchState
    {
        bool touching{};
        bool pressed{};         // 本帧刚按下
        bool released{};        // 本帧刚抬起
        int x{}, y{};
        int start_x{}, start_y{};
        int delta_x{}, delta_y{};   // 相对按下位置的累计位移
        int step_x{}, step_y{};     // 相对上一帧的位移，拖动滚动用

        // 位移超过阈值就认为是拖动而不是点击。
        // 抬手时用它区分"点了一下"和"划了一下"，避免滑动列表时误触发播放。
        static const int kDragThreshold = 16;
        bool IsDrag() const
        {
            return delta_x * delta_x + delta_y * delta_y > kDragThreshold * kDragThreshold;
        }
    };

    void Init();
    // 每帧开头调用一次
    void Update(double delta_seconds);

    bool IsDown(Button button) const { return m_down[button]; }         // 本帧刚按下
    bool IsHeld(Button button) const { return m_held[button]; }         // 持续按住
    bool IsUp(Button button) const { return m_up[button]; }             // 本帧刚抬起
    // 按住时按固定间隔重复触发，用于列表长按滚动
    bool IsRepeat(Button button) const { return m_repeat[button]; }

    // 摇杆归一化到 -1.0~1.0，已应用死区
    float GetLeftStickX() const { return m_left_x; }
    float GetLeftStickY() const { return m_left_y; }
    float GetRightStickX() const { return m_right_x; }
    float GetRightStickY() const { return m_right_y; }

    // 触摸开关关掉时返回一个全空的状态，各界面无需关心这件事就自动失效了。
    // 顶栏那个"重新打开触摸"的按钮必须用 GetRawTouch()，否则关掉之后就再也开不回来。
    const TouchState& GetTouch() const { return m_touch_enabled ? m_touch : m_empty_touch; }
    const TouchState& GetRawTouch() const { return m_touch; }

    void SetTouchEnabled(bool enabled) { m_touch_enabled = enabled; }
    bool IsTouchEnabled() const { return m_touch_enabled; }

    // 本帧是否有任何操作（按键、摇杆或触摸），用于自动变暗的空闲计时
    bool HasAnyInput() const { return m_has_any_input; }

    // 系统请求退出（按下 HOME 后 applet 结束）
    bool ShouldExit() const { return m_should_exit; }

private:
    bool m_down[BTN_COUNT]{};
    bool m_held[BTN_COUNT]{};
    bool m_up[BTN_COUNT]{};
    bool m_repeat[BTN_COUNT]{};
    double m_hold_time[BTN_COUNT]{};

    float m_left_x{}, m_left_y{};
    float m_right_x{}, m_right_y{};
    TouchState m_touch;
    TouchState m_empty_touch;       // GetTouch() 在触摸被禁用时返回它
    bool m_touch_enabled{ true };
    bool m_has_any_input{};
    bool m_should_exit{};
};
