#include "AudioDuration.h"
#include "FileUtil.h"
#include "StringUtil.h"

#include <cstdio>

namespace
{
    inline unsigned char Byte(const std::string& data, size_t index)
    {
        return static_cast<unsigned char>(data[index]);
    }

    uint32_t ReadBE32(const std::string& data, size_t offset)
    {
        return (static_cast<uint32_t>(Byte(data, offset)) << 24)
             | (static_cast<uint32_t>(Byte(data, offset + 1)) << 16)
             | (static_cast<uint32_t>(Byte(data, offset + 2)) << 8)
             |  static_cast<uint32_t>(Byte(data, offset + 3));
    }

    uint32_t ReadLE32(const std::string& data, size_t offset)
    {
        return  static_cast<uint32_t>(Byte(data, offset))
             | (static_cast<uint32_t>(Byte(data, offset + 1)) << 8)
             | (static_cast<uint32_t>(Byte(data, offset + 2)) << 16)
             | (static_cast<uint32_t>(Byte(data, offset + 3)) << 24);
    }

    uint64_t ReadLE64(const std::string& data, size_t offset)
    {
        uint64_t value = 0;
        for (int i = 7; i >= 0; --i)
            value = (value << 8) | Byte(data, offset + static_cast<size_t>(i));
        return value;
    }

    // ID3v2 之后音频数据的起始偏移。没有 ID3v2 就是 0。
    size_t SkipId3v2(const std::string& data)
    {
        if (data.size() < 10 || data.compare(0, 3, "ID3") != 0)
            return 0;
        // 长度是 syncsafe 的：每字节只用低 7 位
        size_t size = (static_cast<size_t>(Byte(data, 6) & 0x7F) << 21)
                    | (static_cast<size_t>(Byte(data, 7) & 0x7F) << 14)
                    | (static_cast<size_t>(Byte(data, 8) & 0x7F) << 7)
                    |  static_cast<size_t>(Byte(data, 9) & 0x7F);
        size_t offset = 10 + size;
        if ((Byte(data, 5) & 0x10) != 0)        // 带页脚的话再跳 10 字节
            offset += 10;
        return offset;
    }

    struct Mp3Frame
    {
        int bitrate_kbps{};
        int sample_rate{};
        int samples_per_frame{};
        int frame_bytes{};
        bool mpeg1{};
        bool mono{};
    };

    // 解析一个 MP3 帧头（4 字节）。不是合法帧头时返回 false。
    bool ParseMp3Header(const std::string& data, size_t offset, Mp3Frame& out)
    {
        if (offset + 4 > data.size())
            return false;
        unsigned char b0 = Byte(data, offset), b1 = Byte(data, offset + 1);
        unsigned char b2 = Byte(data, offset + 2), b3 = Byte(data, offset + 3);
        if (b0 != 0xFF || (b1 & 0xE0) != 0xE0)
            return false;

        const int version_id = (b1 >> 3) & 0x03;    // 0=MPEG2.5 1=保留 2=MPEG2 3=MPEG1
        const int layer_id = (b1 >> 1) & 0x03;      // 1=LayerIII 2=LayerII 3=LayerI
        const int bitrate_index = (b2 >> 4) & 0x0F;
        const int sample_index = (b2 >> 2) & 0x03;
        const int padding = (b2 >> 1) & 0x01;
        const int channel_mode = (b3 >> 6) & 0x03;  // 3=单声道

        if (version_id == 1 || layer_id == 0)
            return false;
        // 0 是"自由码率"、15 是非法，两者都算不出时长
        if (bitrate_index == 0 || bitrate_index == 15 || sample_index == 3)
            return false;

        static const int kBitrateV1L1[15] = { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 };
        static const int kBitrateV1L2[15] = { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 };
        static const int kBitrateV1L3[15] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
        static const int kBitrateV2L1[15] = { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256 };
        static const int kBitrateV2L23[15] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 };

        const bool mpeg1 = (version_id == 3);
        const int* table;
        if (mpeg1)
            table = (layer_id == 3) ? kBitrateV1L1 : (layer_id == 2 ? kBitrateV1L2 : kBitrateV1L3);
        else
            table = (layer_id == 3) ? kBitrateV2L1 : kBitrateV2L23;
        out.bitrate_kbps = table[bitrate_index];

        static const int kRateV1[3] = { 44100, 48000, 32000 };
        static const int kRateV2[3] = { 22050, 24000, 16000 };
        static const int kRateV25[3] = { 11025, 12000, 8000 };
        const int* rates = mpeg1 ? kRateV1 : (version_id == 2 ? kRateV2 : kRateV25);
        out.sample_rate = rates[sample_index];

        if (layer_id == 3)                          // Layer I
            out.samples_per_frame = 384;
        else if (layer_id == 2)                     // Layer II
            out.samples_per_frame = 1152;
        else                                        // Layer III
            out.samples_per_frame = mpeg1 ? 1152 : 576;

        if (layer_id == 3)
            out.frame_bytes = (12 * out.bitrate_kbps * 1000 / out.sample_rate + padding) * 4;
        else
            out.frame_bytes = out.samples_per_frame / 8 * out.bitrate_kbps * 1000 / out.sample_rate
                            + padding;

        out.mpeg1 = mpeg1;
        out.mono = (channel_mode == 3);
        return out.frame_bytes > 0;
    }

    // 从第一个帧的帧体里找 Xing / Info / VBRI，取出总帧数。没有则返回 0。
    uint32_t ReadVbrFrameCount(const std::string& data, size_t frame_offset, const Mp3Frame& frame)
    {
        // Xing/Info 的位置由版本和声道数决定，但直接在帧内搜更省事也更宽容：
        // 帧头之后 4~40 字节的范围里出现这四个字节就是它。
        const size_t limit = frame_offset + static_cast<size_t>(frame.frame_bytes);
        const size_t search_end = (limit < data.size() ? limit : data.size());

        for (size_t i = frame_offset + 4; i + 12 <= search_end && i <= frame_offset + 40; ++i)
        {
            const bool is_xing = (data.compare(i, 4, "Xing") == 0 || data.compare(i, 4, "Info") == 0);
            if (!is_xing)
                continue;
            uint32_t flags = ReadBE32(data, i + 4);
            if ((flags & 0x01) == 0)                // 没有帧数字段
                return 0;
            return ReadBE32(data, i + 8);
        }

        // VBRI 固定在帧头之后 32 字节处，帧数在其内部偏移 14
        const size_t vbri = frame_offset + 36;
        if (vbri + 18 <= data.size() && data.compare(vbri, 4, "VBRI") == 0)
            return ReadBE32(data, vbri + 14);

        return 0;
    }
}

int AudioDuration::Mp3DurationMs(const std::string& head, uint64_t file_size)
{
    const size_t offset = SkipId3v2(head);
    if (offset >= head.size())
        return 0;                                   // 头部缓冲不够大，读不到第一个音频帧
    return Mp3DurationFromAudio(head.substr(offset), offset, file_size);
}

int AudioDuration::Mp3DurationFromAudio(const std::string& audio, uint64_t audio_offset,
                                        uint64_t file_size)
{
    // 找第一个能自洽的帧头。个别文件在 ID3v2 之后还有垃圾字节，所以要往后扫一段；
    // 只认单个同步字容易撞上残留数据里的 0xFF，因此要求下一帧头也对得上。
    Mp3Frame frame;
    size_t frame_offset = 0;
    bool found = false;
    for (size_t i = 0; i + 4 <= audio.size() && i < 8192; ++i)
    {
        if (Byte(audio, i) != 0xFF)
            continue;
        if (!ParseMp3Header(audio, i, frame))
            continue;
        // 声称的这一帧必须真的能装进文件里。缺了这一条，末尾几十个字节的垃圾
        // 数据里凑巧出现一个合法帧头时，会被当成一首几毫秒的歌。
        if (audio_offset + i + static_cast<uint64_t>(frame.frame_bytes) > file_size)
            continue;

        const size_t next = i + static_cast<size_t>(frame.frame_bytes);
        Mp3Frame ignored;
        // 下一帧还在缓冲区内就顺带验一下；已经超出缓冲区就只能信这一个
        if (next + 4 <= audio.size() && !ParseMp3Header(audio, next, ignored))
            continue;
        frame_offset = i;
        found = true;
        break;
    }
    if (!found || frame.sample_rate <= 0)
        return 0;

    const uint32_t frames = ReadVbrFrameCount(audio, frame_offset, frame);
    if (frames > 0)
    {
        const double seconds =
            static_cast<double>(frames) * frame.samples_per_frame / frame.sample_rate;
        return static_cast<int>(seconds * 1000.0);
    }

    // 没有 VBR 头就按定码率估算。VBR 文件缺了 Xing 头时这个值会偏，
    // 但总比进度条上一个 "-:--" 强。
    const uint64_t audio_start = audio_offset + frame_offset;
    if (file_size <= audio_start)
        return 0;
    const uint64_t audio_bytes = file_size - audio_start;
    return static_cast<int>(audio_bytes * 8 / static_cast<uint64_t>(frame.bitrate_kbps));
}

int AudioDuration::WavDurationMs(const std::string& head, uint64_t file_size)
{
    if (head.size() < 12 || head.compare(0, 4, "RIFF") != 0 || head.compare(8, 4, "WAVE") != 0)
        return 0;

    uint32_t byte_rate = 0;
    size_t offset = 12;
    while (offset + 8 <= head.size())
    {
        const std::string id = head.substr(offset, 4);
        uint32_t size = ReadLE32(head, offset + 4);
        const size_t body = offset + 8;

        if (id == "fmt " && body + 16 <= head.size())
        {
            byte_rate = ReadLE32(head, body + 8);
        }
        else if (id == "data")
        {
            if (byte_rate == 0)
                return 0;
            // data 长度为 0 或明显越界时（边写边播的流），按文件实际剩余长度算
            uint64_t data_bytes = size;
            if (data_bytes == 0 || body + data_bytes > file_size)
                data_bytes = (file_size > body) ? file_size - body : 0;
            return static_cast<int>(data_bytes * 1000 / byte_rate);
        }

        offset = body + size + (size & 1);          // 块长度为奇数时有一个填充字节
        if (size == 0)
            break;
    }
    return 0;
}

int AudioDuration::OggDurationMs(const std::string& head, const std::string& tail)
{
    if (head.size() < 4 || head.compare(0, 4, "OggS") != 0)
        return 0;

    // 首页里是识别头，从中取采样率。Opus 的 granule 恒定按 48kHz 计，
    // 但要减掉 pre-skip；Vorbis 直接用它自己声明的采样率。
    int sample_rate = 0;
    uint32_t pre_skip = 0;
    const size_t scan = (head.size() < 4096) ? head.size() : 4096;
    // 上界按每个分支自己要读到的最远字节算。写成一个统一的常数看着简洁，
    // 但 Ogg 页头本身就占 28 字节，识别头短的文件会被整个跳过去。
    for (size_t i = 0; i + 8 <= scan; ++i)
    {
        if (i + 12 <= scan && head.compare(i, 8, "OpusHead") == 0)
        {
            sample_rate = 48000;
            pre_skip = static_cast<uint32_t>(Byte(head, i + 10))
                     | (static_cast<uint32_t>(Byte(head, i + 11)) << 8);
            break;
        }
        if (i + 16 <= scan && head.compare(i, 7, std::string(1, '\x01') + "vorbis") == 0)
        {
            sample_rate = static_cast<int>(ReadLE32(head, i + 12));
            break;
        }
    }
    if (sample_rate <= 0)
        return 0;

    // 最后一个 Ogg 页的 granule position 就是流的总采样数
    uint64_t granule = 0;
    bool found = false;
    for (size_t i = 0; i + 14 <= tail.size(); ++i)
    {
        if (tail.compare(i, 4, "OggS") != 0)
            continue;
        granule = ReadLE64(tail, i + 6);
        found = true;                               // 继续往后找，要的是最后一个
    }
    if (!found || granule == 0 || granule == static_cast<uint64_t>(-1))
        return 0;

    if (granule > pre_skip)
        granule -= pre_skip;
    return static_cast<int>(granule * 1000 / static_cast<uint64_t>(sample_rate));
}

int AudioDuration::FlacDurationMs(const std::string& head)
{
    if (head.size() < 4 || head.compare(0, 4, "fLaC") != 0)
        return 0;
    // 第一个元数据块必定是 STREAMINFO：4 字节块头 + 34 字节块体
    if (head.size() < 4 + 4 + 34)
        return 0;
    if ((Byte(head, 4) & 0x7F) != 0)                // 块类型 0 才是 STREAMINFO
        return 0;

    // 块体偏移 10 起是 20 位采样率、3 位声道、5 位位深、36 位总采样数
    const size_t body = 8;
    uint64_t bits = 0;
    for (size_t i = 0; i < 8; ++i)
        bits = (bits << 8) | Byte(head, body + 10 + i);

    const uint32_t sample_rate = static_cast<uint32_t>(bits >> 44) & 0xFFFFF;
    const uint64_t total_samples = bits & 0xFFFFFFFFFULL;
    if (sample_rate == 0 || total_samples == 0)
        return 0;
    return static_cast<int>(total_samples * 1000 / sample_rate);
}

int AudioDuration::EstimateFromBuffers(const std::string& ext, const std::string& head,
                                       const std::string& tail, uint64_t file_size)
{
    if (ext == "mp3")
        return Mp3DurationMs(head, file_size);
    if (ext == "wav" || ext == "wave")
        return WavDurationMs(head, file_size);
    if (ext == "ogg" || ext == "oga" || ext == "opus")
        return OggDurationMs(head, tail);
    if (ext == "flac")
        return FlacDurationMs(head);
    return 0;
}

int AudioDuration::Estimate(const std::string& file_path)
{
    const std::string ext = StringUtil::ToLower(FileUtil::GetExtension(file_path));
    if (ext.empty())
        return 0;

    std::string head;
    if (!FileUtil::ReadHead(file_path, kHeadBytes, head) || head.empty())
        return 0;

    const uint64_t file_size = FileUtil::GetFileSize(file_path);

    // MP3 的 ID3v2 标签里塞着封面时能有几百 KB，第一个音频帧就落在头部缓冲之外。
    // 标签长度写在最开头的 10 字节里，直接照它跳过去读那一段即可。
    if (ext == "mp3")
    {
        const size_t audio_offset = SkipId3v2(head);
        if (audio_offset < head.size())
            return Mp3DurationMs(head, file_size);

        std::string audio;
        if (!FileUtil::ReadRange(file_path, audio_offset, 16 * 1024, audio))
            return 0;
        return Mp3DurationFromAudio(audio, audio_offset, file_size);
    }

    // Ogg 要靠末尾那一页拿总采样数，其余格式读头就够了
    std::string tail;
    if (ext == "ogg" || ext == "oga" || ext == "opus")
    {
        // ReadTail 在文件比请求长度还短时会失败，那种情况下 head 本身就是整个文件
        if (!FileUtil::ReadTail(file_path, kTailBytes, tail))
            tail = head;
    }

    return EstimateFromBuffers(ext, head, tail, file_size);
}
