#pragma once
#include "SpectrumAnalyzer.h"
#include <string>

typedef struct _Mix_Music Mix_Music;

// 基于 SDL2_mixer 的播放内核。
//
// 桌面版通过 IPlayerCore 抽象出 BASS / FFmpeg 两套内核；Switch 上没有 BASS，
// 这里用 SDL2_mixer 实现同一组能力。devkitPro 的 switch-sdl2_mixer 2.0.4 实际带的
// 解码器是 mpg123 / vorbisidec / opusfile / modplug / timidity / wav（不含 FLAC）。
// 相比桌面版缺少的功能：FLAC、音效（均衡器、混响）、变速播放、cue 音轨内定位。
class CAudioEngine
{
public:
    enum PlayingState
    {
        PS_STOPPED = 0,
        PS_PLAYING,
        PS_PAUSED
    };

    CAudioEngine();
    ~CAudioEngine();

    bool Init();
    void Uninit();
    bool IsInited() const { return m_inited; }

    // 打开并立即播放。失败时返回 false 且状态回到 PS_STOPPED。
    bool Open(const std::string& file_path);
    void Close();

    void Play();
    void Pause();
    void TogglePause();
    void Stop();

    PlayingState GetState() const;
    bool IsPlaying() const { return GetState() == PS_PLAYING; }

    // 当前播放位置（毫秒）。SDL2_mixer 无法为所有格式提供精确位置，
    // 因此这里以“定位基准 + 自行累计的经过时间”为准。
    int  GetCurrentPosition() const;
    int  GetSongLength() const;             // 未知时返回 0
    bool SetPosition(int ms);               // 绝对定位
    bool Seek(int delta_ms);                // 相对定位

    int  GetVolume() const { return m_volume; }
    void SetVolume(int volume);             // 0~100
    void AdjustVolume(int delta);

    // 每帧调用一次：推进位置计时、检测自然播放结束
    void Update();
    // 上一首播放是否已自然结束（读取后清除标志）
    bool TakeSongFinished();

    CSpectrumAnalyzer& GetSpectrum() { return m_spectrum; }

    const std::string& GetLastError() const { return m_last_error; }

private:
    static void PostMixCallback(void* udata, unsigned char* stream, int len);
    static void MusicFinishedCallback();

    bool m_inited{};
    Mix_Music* m_music{};
    std::string m_file_path;
    std::string m_last_error;

    int m_volume{ 80 };
    int m_length_ms{};
    int m_seek_base_ms{};                   // 最近一次定位对应的曲内位置
    unsigned int m_seek_tick{};             // 那次定位时的 SDL_GetTicks()
    int m_paused_position_ms{};

    int m_mix_channels{ 2 };
    CSpectrumAnalyzer m_spectrum;
};
