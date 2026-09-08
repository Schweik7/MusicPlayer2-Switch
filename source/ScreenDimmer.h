#pragma once

// 空闲一段时间后自动调暗屏幕，省电。
//
// 用 lbl 服务真的去改背光亮度，而不是在画面上盖一层黑色：
// 这台机器是 LCD，盖黑色一点电都不省。
//
// 亮度设置是全局的、退出后仍然生效，所以初始化时必须记下用户原来的值，
// 唤醒和退出时都要还回去。自动亮度如果开着会覆盖我们设的值，
// 因此调暗期间临时关掉它，恢复时再打开。
class CScreenDimmer
{
public:
    void Init();
    void Uninit();

    // 每帧调用。any_input 表示这一帧有按键或触摸；
    // keep_awake 为 true 时不进入调暗（比如正在浏览文件）。
    void Update(double delta_seconds, bool any_input, bool allow_dim);

    bool IsDimmed() const { return m_dimmed; }
    // 距离自动调暗还有多久；未启用或已调暗时返回 0
    double GetRemainingSeconds() const;

    void SetTimeoutSeconds(double seconds) { m_timeout = seconds; }
    double GetTimeoutSeconds() const { return m_timeout; }
    // 0 表示关闭该功能
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }

private:
    void Dim();
    void Restore();

    bool m_inited{};
    bool m_enabled{ true };
    bool m_dimmed{};
    double m_idle{};
    double m_timeout{ 60.0 };

    float m_saved_brightness{ 1.0f };
    bool m_saved_auto_brightness{};
    bool m_have_saved{};
};
