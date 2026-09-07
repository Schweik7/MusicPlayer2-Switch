#pragma once
#include "AudioRingBuffer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// FLAC__Frame / FLAC__StreamMetadata 在 libFLAC 里是匿名结构体的 typedef，
// 没法前向声明，所以这里干脆不暴露任何 FLAC 类型：解码器句柄用 void*，
// C 回调放在 .cpp 里做成静态函数，只把拆好的参数转发给下面几个 On* 成员。
typedef struct _SDL_AudioStream SDL_AudioStream;

// FLAC 流式解码器。
//
// 为什么需要它：devkitPro 的 switch-sdl2_mixer 2.0.4 没有编译 FLAC 解码器
// （nm 查 Mix_MusicInterface_* 里没有 FLAC），而 libFLAC 本身是有的。
// 所以这里自己解一路 PCM，通过 Mix_HookMusic 直接喂给混音器。
//
// 数据流：
//   解码线程: libFLAC 解出 int32 分声道样本
//             -> 转成 int16 交错
//             -> SDL_AudioStream 重采样/转声道到输出格式
//             -> 写入环形缓冲区
//   音频回调: 从环形缓冲区读走（不做任何可能阻塞的事）
//
// 解码放在独立线程而不是音频回调里，是因为 SD 卡的读取延迟会有毫秒级尖峰，
// 在回调里做文件 IO 会造成爆音。
class CFlacDecoder
{
public:
    CFlacDecoder();
    ~CFlacDecoder();

    // 打开文件并按 out_rate / out_channels 输出 S16 交错 PCM
    bool Open(const std::string& path, int out_rate, int out_channels);
    void Close();
    bool IsOpen() const { return m_open; }

    // 由音频回调调用。数据不足时余下部分填静音，返回值恒等于 bytes。
    // 这个函数不加锁等待，也不做文件 IO。
    size_t Read(uint8_t* out, size_t bytes);

    bool Seek(int ms);

    int  GetPositionMs() const;
    int  GetLengthMs() const { return m_length_ms; }
    // 解码到文件末尾且缓冲区已排空
    bool IsFinished() const;

    // 源文件的原始参数，用于界面显示
    int GetSourceSampleRate() const { return m_src_rate; }
    int GetSourceChannels() const { return m_src_channels; }
    int GetSourceBitsPerSample() const { return m_src_bps; }

    const std::string& GetLastError() const { return m_last_error; }

    // 以下三个由 .cpp 里的 libFLAC C 回调调用，不是给外部用的
    void OnWrite(const int32_t* const* buffer, int channels, int samples, int bps);
    void OnStreamInfo(int sample_rate, int channels, int bps, uint64_t total_samples);
    void OnDecodeError();

private:
    void DecodeLoop();
    void StopThread();
    // 把 SDL_AudioStream 里已转换好的数据取出来放进 m_pending
    void DrainAudioStream();
    // 尝试把 m_pending 写入环形缓冲区，返回是否已全部写完
    bool FlushPendingToRing(uint32_t generation);
    void HandleSeek();

    void* m_decoder{};                  // 实为 FLAC__StreamDecoder*
    SDL_AudioStream* m_stream{};
    CAudioRingBuffer m_ring;

    bool m_open{};
    std::string m_path;
    std::string m_last_error;

    // 源格式
    int m_src_rate{};
    int m_src_channels{};
    int m_src_bps{};
    uint64_t m_total_samples{};
    int m_length_ms{};

    // 输出格式
    int m_out_rate{ 48000 };
    int m_out_channels{ 2 };
    int m_out_frame_bytes{ 4 };         // out_channels * sizeof(int16)

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_eof{ false };
    // 每次 seek 自增，用来丢弃 seek 之前解出来、还没写进环形缓冲的数据
    std::atomic<uint32_t> m_generation{ 0 };

    std::mutex m_seek_mutex;
    std::condition_variable m_seek_cv;
    bool m_seek_pending{};
    int  m_seek_target_ms{};

    // 已交给音频回调的输出帧数，配合 m_seek_base_ms 换算播放位置
    std::atomic<uint64_t> m_frames_played{ 0 };
    std::atomic<int> m_seek_base_ms{ 0 };

    // 解出来但还没写进环形缓冲的字节（环形缓冲满时会积压在这里）
    std::vector<uint8_t> m_pending;
    // 写回调把转换后的 int16 暂存到这里再交给 SDL_AudioStream
    std::vector<int16_t> m_convert_buffer;
};
