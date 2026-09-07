#include "FlacDecoder.h"
#include "../core/FileUtil.h"

#include <SDL2/SDL.h>
#include <FLAC/stream_decoder.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace
{
    // 环形缓冲区容量：48kHz 立体声 S16 是 192000 字节/秒，这里约 0.7 秒。
    // 太小会在 SD 卡卡顿时爆音，太大则 seek 后的响应变慢（seek 会把它整个丢掉）。
    const size_t kRingCapacity = 128 * 1024;

    // 环形缓冲区剩余空间低于这个值时解码线程先歇一下，避免空转
    const size_t kDecodeThreshold = 8 * 1024;

    const int kIdleSleepMs = 4;

    int16_t ClampToInt16(int32_t value)
    {
        if (value > 32767) return 32767;
        if (value < -32768) return -32768;
        return static_cast<int16_t>(value);
    }

    // 头文件里把解码器句柄存成 void*（FLAC 的类型没法前向声明），这里还原回去
    inline FLAC__StreamDecoder* Dec(void* handle)
    {
        return static_cast<FLAC__StreamDecoder*>(handle);
    }
}

CFlacDecoder::CFlacDecoder()
    : m_ring(0)
{
}

CFlacDecoder::~CFlacDecoder()
{
    Close();
}

// ---------------------------------------------------------------- libFLAC 回调
//
// 这些是 C 接口的回调，只负责把参数拆开转发给 CFlacDecoder 的成员函数，
// 这样 FLAC 的类型就不必出现在头文件里。

namespace
{
    FLAC__StreamDecoderWriteStatus WriteCallback(const FLAC__StreamDecoder* decoder,
                                                 const FLAC__Frame* frame,
                                                 const FLAC__int32* const buffer[],
                                                 void* client_data)
    {
        (void)decoder;
        CFlacDecoder* self = static_cast<CFlacDecoder*>(client_data);
        self->OnWrite(buffer,
                      static_cast<int>(frame->header.channels),
                      static_cast<int>(frame->header.blocksize),
                      static_cast<int>(frame->header.bits_per_sample));
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    void MetadataCallback(const FLAC__StreamDecoder* decoder,
                          const FLAC__StreamMetadata* metadata, void* client_data)
    {
        (void)decoder;
        if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO)
            return;
        const FLAC__StreamMetadata_StreamInfo& info = metadata->data.stream_info;
        CFlacDecoder* self = static_cast<CFlacDecoder*>(client_data);
        self->OnStreamInfo(static_cast<int>(info.sample_rate),
                           static_cast<int>(info.channels),
                           static_cast<int>(info.bits_per_sample),
                           info.total_samples);
    }

    void ErrorCallback(const FLAC__StreamDecoder* decoder,
                       FLAC__StreamDecoderErrorStatus status, void* client_data)
    {
        (void)decoder;
        (void)status;
        static_cast<CFlacDecoder*>(client_data)->OnDecodeError();
    }
}

void CFlacDecoder::OnStreamInfo(int sample_rate, int channels, int bps, uint64_t total_samples)
{
    m_src_rate = sample_rate;
    m_src_channels = channels;
    m_src_bps = bps;
    m_total_samples = total_samples;
}

void CFlacDecoder::OnDecodeError()
{
    // 单帧解码错误不致命：libFLAC 会自己跳到下一个同步点继续，
    // 这里只记下来，不打断播放
    if (m_last_error.empty())
        m_last_error = "FLAC 数据中存在损坏的帧，已跳过";
}

void CFlacDecoder::OnWrite(const int32_t* const* buffer, int channels, int samples, int bps)
{
    if (buffer == nullptr || channels <= 0 || samples <= 0)
        return;

    // FLAC 给的是分声道的 int32，按源位深归一化到 int16 再交错
    const int shift = bps - 16;
    m_convert_buffer.resize(static_cast<size_t>(samples) * channels);

    for (int i = 0; i < samples; ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            int32_t sample = buffer[ch][i];
            if (shift > 0)
                sample >>= shift;               // 24bit / 32bit 源降到 16bit
            else if (shift < 0)
                sample <<= -shift;              // 8bit / 12bit 源升到 16bit
            m_convert_buffer[static_cast<size_t>(i) * channels + ch] = ClampToInt16(sample);
        }
    }

    if (m_stream != nullptr)
    {
        SDL_AudioStreamPut(m_stream, m_convert_buffer.data(),
                           static_cast<int>(m_convert_buffer.size() * sizeof(int16_t)));
    }
}

// -------------------------------------------------------------------- 打开关闭

bool CFlacDecoder::Open(const std::string& path, int out_rate, int out_channels)
{
    Close();

    m_last_error.clear();
    if (!FileUtil::Exists(path))
    {
        m_last_error = "文件不存在: " + path;
        return false;
    }

    FLAC__StreamDecoder* decoder = FLAC__stream_decoder_new();
    if (decoder == nullptr)
    {
        m_last_error = "FLAC__stream_decoder_new 失败";
        return false;
    }
    m_decoder = decoder;

    // 关掉 MD5 校验：它要求把整个文件读完才能验证，对流式播放没有意义
    FLAC__stream_decoder_set_md5_checking(decoder, false);

    FLAC__StreamDecoderInitStatus status = FLAC__stream_decoder_init_file(
        decoder, path.c_str(), &WriteCallback, &MetadataCallback, &ErrorCallback, this);
    if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK)
    {
        m_last_error = std::string("无法打开 FLAC 文件: ")
                     + FLAC__StreamDecoderInitStatusString[status];
        FLAC__stream_decoder_delete(decoder);
        m_decoder = nullptr;
        return false;
    }

    // 先把元数据读完，拿到采样率/声道数/位深，才能建立转换流
    if (!FLAC__stream_decoder_process_until_end_of_metadata(decoder))
    {
        m_last_error = "读取 FLAC 元数据失败";
        FLAC__stream_decoder_finish(decoder);
        FLAC__stream_decoder_delete(decoder);
        m_decoder = nullptr;
        return false;
    }

    if (m_src_rate <= 0 || m_src_channels <= 0)
    {
        m_last_error = "FLAC 文件缺少有效的 STREAMINFO";
        FLAC__stream_decoder_finish(decoder);
        FLAC__stream_decoder_delete(decoder);
        m_decoder = nullptr;
        return false;
    }

    m_out_rate = out_rate;
    m_out_channels = out_channels;
    m_out_frame_bytes = out_channels * static_cast<int>(sizeof(int16_t));

    m_length_ms = (m_total_samples > 0)
        ? static_cast<int>(m_total_samples * 1000ULL / static_cast<uint64_t>(m_src_rate))
        : 0;

    // SDL_AudioStream 负责重采样 + 声道数转换（FLAC 常见 44.1kHz，输出是 48kHz）
    m_stream = SDL_NewAudioStream(AUDIO_S16SYS, static_cast<uint8_t>(m_src_channels), m_src_rate,
                                  AUDIO_S16SYS, static_cast<uint8_t>(m_out_channels), m_out_rate);
    if (m_stream == nullptr)
    {
        m_last_error = std::string("SDL_NewAudioStream 失败: ") + SDL_GetError();
        FLAC__stream_decoder_finish(Dec(m_decoder));
        FLAC__stream_decoder_delete(Dec(m_decoder));
        m_decoder = nullptr;
        return false;
    }

    m_ring.Reset(kRingCapacity);
    m_pending.clear();
    m_path = path;
    m_eof.store(false);
    m_frames_played.store(0);
    m_seek_base_ms.store(0);
    m_generation.store(0);
    m_seek_pending = false;
    m_open = true;

    m_running.store(true);
    m_thread = std::thread(&CFlacDecoder::DecodeLoop, this);
    return true;
}

void CFlacDecoder::StopThread()
{
    if (!m_thread.joinable())
        return;
    m_running.store(false);
    m_seek_cv.notify_all();
    m_thread.join();
}

void CFlacDecoder::Close()
{
    StopThread();

    if (m_decoder != nullptr)
    {
        FLAC__stream_decoder_finish(Dec(m_decoder));
        FLAC__stream_decoder_delete(Dec(m_decoder));
        m_decoder = nullptr;
    }
    if (m_stream != nullptr)
    {
        SDL_FreeAudioStream(m_stream);
        m_stream = nullptr;
    }

    m_ring.Reset(0);
    m_pending.clear();
    m_convert_buffer.clear();
    m_open = false;
    m_path.clear();
    m_src_rate = 0;
    m_src_channels = 0;
    m_src_bps = 0;
    m_total_samples = 0;
    m_length_ms = 0;
    m_eof.store(false);
    m_frames_played.store(0);
    m_seek_base_ms.store(0);
}

// -------------------------------------------------------------------- 解码线程

void CFlacDecoder::DrainAudioStream()
{
    if (m_stream == nullptr)
        return;

    uint8_t chunk[8192];
    while (true)
    {
        int available = SDL_AudioStreamAvailable(m_stream);
        if (available <= 0)
            break;
        int got = SDL_AudioStreamGet(m_stream, chunk, static_cast<int>(sizeof(chunk)));
        if (got <= 0)
            break;
        m_pending.insert(m_pending.end(), chunk, chunk + got);
    }
}

bool CFlacDecoder::FlushPendingToRing(uint32_t generation)
{
    if (m_pending.empty())
        return true;

    // seek 之后，seek 之前解出来的数据必须丢掉，否则会先放一段旧音频
    if (m_generation.load() != generation)
    {
        m_pending.clear();
        return true;
    }

    size_t written = m_ring.Write(m_pending.data(), m_pending.size());
    if (written == m_pending.size())
    {
        m_pending.clear();
        return true;
    }
    m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<long>(written));
    return false;
}

void CFlacDecoder::HandleSeek()
{
    int target_ms;
    {
        std::lock_guard<std::mutex> lock(m_seek_mutex);
        if (!m_seek_pending)
            return;
        m_seek_pending = false;
        target_ms = m_seek_target_ms;
    }

    uint64_t target_sample = static_cast<uint64_t>(target_ms) * static_cast<uint64_t>(m_src_rate) / 1000ULL;
    if (m_total_samples > 0 && target_sample >= m_total_samples)
        target_sample = m_total_samples > 0 ? m_total_samples - 1 : 0;

    // 丢掉所有在途数据：转换流里的、积压的、环形缓冲里的
    m_pending.clear();
    if (m_stream != nullptr)
        SDL_AudioStreamClear(m_stream);
    m_ring.Clear();

    if (FLAC__stream_decoder_seek_absolute(Dec(m_decoder), target_sample))
    {
        m_eof.store(false);
    }
    else
    {
        // 定位失败会让解码器进入 SEEK_ERROR 状态，必须 flush 才能继续用
        FLAC__stream_decoder_flush(Dec(m_decoder));
        m_last_error = "FLAC 定位失败";
    }

    m_seek_base_ms.store(target_ms);
    m_frames_played.store(0);
}

void CFlacDecoder::DecodeLoop()
{
    while (m_running.load())
    {
        HandleSeek();
        if (!m_running.load())
            break;

        uint32_t generation = m_generation.load();

        // 先把上一轮没写完的数据补进环形缓冲
        if (!FlushPendingToRing(generation))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kIdleSleepMs));
            continue;
        }

        if (m_eof.load())
        {
            // 已经解完，等着被 seek 或者关闭
            std::unique_lock<std::mutex> lock(m_seek_mutex);
            m_seek_cv.wait_for(lock, std::chrono::milliseconds(50),
                               [this]() { return m_seek_pending || !m_running.load(); });
            continue;
        }

        if (m_ring.Space() < kDecodeThreshold)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kIdleSleepMs));
            continue;
        }

        // 解一帧。写回调会把 PCM 推进 SDL_AudioStream
        if (!FLAC__stream_decoder_process_single(Dec(m_decoder)))
        {
            m_eof.store(true);
            continue;
        }

        FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(Dec(m_decoder));
        if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
        {
            // 让重采样器把尾巴上的样本吐出来，否则最后几十毫秒会丢
            if (m_stream != nullptr)
                SDL_AudioStreamFlush(m_stream);
            DrainAudioStream();
            FlushPendingToRing(generation);
            m_eof.store(true);
            continue;
        }

        DrainAudioStream();
        FlushPendingToRing(generation);
    }
}

// ---------------------------------------------------------------------- 消费端

size_t CFlacDecoder::Read(uint8_t* out, size_t bytes)
{
    if (out == nullptr || bytes == 0)
        return 0;

    size_t got = m_ring.Read(out, bytes);
    if (got < bytes)
    {
        // 欠载（解码跟不上或已到文件末尾）时补静音，不能把上一轮的内容留在缓冲里
        std::memset(out + got, 0, bytes - got);
    }

    if (m_out_frame_bytes > 0)
        m_frames_played.fetch_add(got / static_cast<size_t>(m_out_frame_bytes));
    return bytes;
}

bool CFlacDecoder::Seek(int ms)
{
    if (!m_open)
        return false;
    if (ms < 0)
        ms = 0;
    if (m_length_ms > 0 && ms > m_length_ms)
        ms = m_length_ms;

    {
        std::lock_guard<std::mutex> lock(m_seek_mutex);
        m_seek_target_ms = ms;
        m_seek_pending = true;
    }

    // 先自增代号再清空缓冲：解码线程据此丢弃 seek 之前解出的在途数据，
    // 否则 seek 后会先播放一段旧音频
    m_generation.fetch_add(1);
    m_ring.Clear();
    // 位置立刻跟上，界面不会出现回跳
    m_seek_base_ms.store(ms);
    m_frames_played.store(0);

    m_seek_cv.notify_all();
    return true;
}

int CFlacDecoder::GetPositionMs() const
{
    if (!m_open || m_out_rate <= 0)
        return 0;
    uint64_t frames = m_frames_played.load();
    int elapsed = static_cast<int>(frames * 1000ULL / static_cast<uint64_t>(m_out_rate));
    int position = m_seek_base_ms.load() + elapsed;
    if (m_length_ms > 0 && position > m_length_ms)
        position = m_length_ms;
    return position;
}

bool CFlacDecoder::IsFinished() const
{
    return m_open && m_eof.load() && m_ring.IsEmpty();
}
