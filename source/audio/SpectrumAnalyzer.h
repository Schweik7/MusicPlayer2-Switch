#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

// 频谱分析。SDL2_mixer 的 post-mix 回调在音频线程里跑，只做最少的事情：
// 把交错的立体声样本降混成单声道塞进环形缓冲区。FFT 放到 UI 线程里算。
class CSpectrumAnalyzer
{
public:
    static const int kFftSize = 512;        // 必须是 2 的幂
    static const int kBarCount = 32;        // 显示的频谱柱数量

    CSpectrumAnalyzer();

    // 由音频线程调用，必须尽量短
    void PushSamples(const int16_t* interleaved, int frame_count, int channels);

    // 由 UI 线程调用：做 FFT 并更新柱高（0.0~1.0，带下落平滑）
    void Update(double delta_seconds);

    const std::array<float, kBarCount>& GetBars() const { return m_bars; }
    void Reset();

private:
    void ComputeFft(const std::vector<float>& input, std::vector<float>& magnitude) const;

    std::mutex m_mutex;
    std::vector<float> m_ring;              // 长度 kFftSize 的环形缓冲
    size_t m_write_pos{};

    std::array<float, kBarCount> m_bars{};
    std::vector<float> m_window;            // 预计算的 Hann 窗
};
