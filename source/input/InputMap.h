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
//   方向键上下  列表移动 / 音量（播放界面）
//   左摇杆      与方向键等价
//   右摇杆左右  拖动进度
//   +          打开设置
//   -          打开/关闭播放列表
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
        BTN_UP,
        BTN_DOWN,
        BTN_LEFT,
        BTN_RIGHT,
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
        int delta_x{}, delta_y{};
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

    const TouchState& GetTouch() const { return m_touch; }

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
    bool m_should_exit{};
};
