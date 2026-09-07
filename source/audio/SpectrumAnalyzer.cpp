#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>

CSpectrumAnalyzer::CSpectrumAnalyzer()
    : m_ring(kFftSize, 0.0f), m_window(kFftSize, 0.0f)
{
    for (int i = 0; i < kFftSize; ++i)
    {
        // Hann 窗，抑制频谱泄漏
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f * i / (kFftSize - 1)));
    }
}

void CSpectrumAnalyzer::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::fill(m_ring.begin(), m_ring.end(), 0.0f);
    m_write_pos = 0;
    m_bars.fill(0.0f);
}

void CSpectrumAnalyzer::PushSamples(const int16_t* interleaved, int frame_count, int channels)
{
    if (interleaved == nullptr || frame_count <= 0 || channels <= 0)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < frame_count; ++i)
    {
        int sum = 0;
        for (int c = 0; c < channels; ++c)
            sum += interleaved[i * channels + c];
        m_ring[m_write_pos] = static_cast<float>(sum) / (channels * 32768.0f);
        m_write_pos = (m_write_pos + 1) % kFftSize;
    }
}

void CSpectrumAnalyzer::ComputeFft(const std::vector<float>& input, std::vector<float>& magnitude) const
{
    const int n = kFftSize;
    std::vector<std::complex<float>> data(n);
    for (int i = 0; i < n; ++i)
        data[i] = std::complex<float>(input[i] * m_window[i], 0.0f);

    // 位反转置换
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }

    // 迭代式 Cooley-Tukey
    for (int len = 2; len <= n; len <<= 1)
    {
        float angle = -2.0f * 3.14159265358979f / len;
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k)
            {
                std::complex<float> u = data[i + k];
                std::complex<float> v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    magnitude.resize(n / 2);
    for (int i = 0; i < n / 2; ++i)
        magnitude[i] = std::abs(data[i]) / (n / 2.0f);
}

void CSpectrumAnalyzer::Update(double delta_seconds)
{
    std::vector<float> samples(kFftSize);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 从写指针处展开成时间有序的一段
        for (int i = 0; i < kFftSize; ++i)
            samples[i] = m_ring[(m_write_pos + i) % kFftSize];
    }

    std::vector<float> magnitude;
    ComputeFft(samples, magnitude);

    // 按对数频率分组：低频柱窄、高频柱宽，视觉上更接近人耳感受
    const int bins = static_cast<int>(magnitude.size());
    const float fall_speed = static_cast<float>(delta_seconds) * 2.2f;

    for (int bar = 0; bar < kBarCount; ++bar)
    {
        float t0 = static_cast<float>(bar) / kBarCount;
        float t1 = static_cast<float>(bar + 1) / kBarCount;
        int lo = static_cast<int>(std::pow(static_cast<float>(bins), t0));
        int hi = static_cast<int>(std::pow(static_cast<float>(bins), t1));
        lo = std::max(1, std::min(lo, bins - 1));
        hi = std::max(lo + 1, std::min(hi, bins));

        float peak = 0.0f;
        for (int i = lo; i < hi; ++i)
            peak = std::max(peak, magnitude[i]);

        // 转成 dB 再归一化到 0~1；-60dB 以下视为静音
        float db = 20.0f * std::log10(peak + 1e-6f);
        float level = (db + 60.0f) / 60.0f;
        level = std::max(0.0f, std::min(1.0f, level));

        // 上升立即跟随，下落做平滑，避免闪烁
        if (level >= m_bars[bar])
            m_bars[bar] = level;
        else
            m_bars[bar] = std::max(level, m_bars[bar] - fall_speed);
    }
}
