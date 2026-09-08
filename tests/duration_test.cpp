// AudioDuration 的单元测试。
//
// 这里全是手工拼出来的容器头：时长解析就是一串偏移量和位域计算，
// 正是最容易写错、又最容易用内存样本覆盖的那类代码。
//
// 合成夹具能覆盖边界，但覆盖不了"真实文件长什么样"——比如内嵌封面把
// ID3v2 撑到 640KB，第一个音频帧根本不在头部缓冲里。那条路径是拿用户的
// 实际音乐文件跑出来才发现的，这里补了对应的回归用例。
#include "TestFramework.h"
#include "../source/core/AudioDuration.h"

#include <cstdio>
#include <initializer_list>
#include <string>

namespace
{
    std::string Bytes(std::initializer_list<int> values)
    {
        std::string result;
        for (int value : values)
            result.push_back(static_cast<char>(value & 0xFF));
        return result;
    }

    std::string BE32(uint32_t value)
    {
        return Bytes({ static_cast<int>(value >> 24), static_cast<int>(value >> 16),
                       static_cast<int>(value >> 8), static_cast<int>(value) });
    }

    std::string LE32(uint32_t value)
    {
        return Bytes({ static_cast<int>(value), static_cast<int>(value >> 8),
                       static_cast<int>(value >> 16), static_cast<int>(value >> 24) });
    }

    std::string LE16(uint32_t value)
    {
        return Bytes({ static_cast<int>(value), static_cast<int>(value >> 8) });
    }

    // MPEG1 Layer III / 44100Hz / 320kbps / 立体声的帧头。
    // 帧长 = 1152/8 * 320000 / 44100 = 1044 字节
    std::string Mp3FrameHeader320()
    {
        return Bytes({ 0xFF, 0xFB, 0xE0, 0x44 });
    }

    // MPEG1 Layer III / 44100Hz / 128kbps / 立体声。帧长 417 字节
    std::string Mp3FrameHeader128()
    {
        return Bytes({ 0xFF, 0xFB, 0x90, 0x44 });
    }

    // 一个完整的 MP3 帧：帧头 + 帧体，补零到帧长
    std::string Mp3Frame(const std::string& header, size_t frame_bytes,
                         const std::string& body = std::string())
    {
        std::string frame = header + body;
        frame.resize(frame_bytes, '\0');
        return frame;
    }

    // 带 syncsafe 长度的 ID3v2.3 头
    std::string Id3v2Header(size_t body_size)
    {
        std::string tag = "ID3";
        tag += Bytes({ 3, 0, 0 });
        tag += Bytes({ static_cast<int>((body_size >> 21) & 0x7F),
                       static_cast<int>((body_size >> 14) & 0x7F),
                       static_cast<int>((body_size >> 7) & 0x7F),
                       static_cast<int>(body_size & 0x7F) });
        return tag;
    }

    // 一个 Ogg 页：只填测试要用到的字段
    std::string OggPage(uint64_t granule, const std::string& payload)
    {
        std::string page = "OggS";
        page += Bytes({ 0, 0 });                        // 版本、页类型
        for (int i = 0; i < 8; ++i)                     // granule position，小端
            page.push_back(static_cast<char>((granule >> (i * 8)) & 0xFF));
        page += LE32(1);                                // 流序列号
        page += LE32(0);                                // 页序号
        page += LE32(0);                                // 校验和
        page += Bytes({ 1, static_cast<int>(payload.size() & 0xFF) });
        page += payload;
        return page;
    }

    // 一个 44100Hz / 立体声 / 16bit 的 FLAC STREAMINFO
    std::string FlacWithSamples(uint64_t total_samples)
    {
        const uint64_t packed = (44100ULL << 44) | (1ULL << 41) | (15ULL << 36) | total_samples;
        std::string flac = "fLaC";
        flac += Bytes({ 0x00, 0x00, 0x00, 0x22 });      // 块类型 0（STREAMINFO），长度 34
        flac += std::string(10, '\0');                  // 块大小 / 帧大小，测试用不到
        for (int i = 7; i >= 0; --i)
            flac.push_back(static_cast<char>((packed >> (i * 8)) & 0xFF));
        flac += std::string(16, '\0');                  // MD5
        return flac;
    }
}

void RunDurationTests()
{
    std::printf("AudioDuration MP3\n");
    {
        // Xing 头带帧数：10000 帧 × 1152 采样 / 44100 = 261.2 秒
        std::string xing = "Xing" + BE32(0x01) + BE32(10000);
        std::string data = Mp3Frame(Mp3FrameHeader320(), 1044, std::string(32, '\0') + xing);
        data += Mp3Frame(Mp3FrameHeader320(), 1044);
        CHECK_NEAR(AudioDuration::Mp3DurationMs(data, data.size()), 261224, 50);

        // Info（定码率文件里的同一种结构）也要认
        std::string info = "Info" + BE32(0x01) + BE32(5000);
        std::string cbr = Mp3Frame(Mp3FrameHeader320(), 1044, std::string(32, '\0') + info);
        cbr += Mp3Frame(Mp3FrameHeader320(), 1044);
        CHECK_NEAR(AudioDuration::Mp3DurationMs(cbr, cbr.size()), 130612, 50);
    }
    {
        // 没有 VBR 头：按码率和音频字节数估。
        // 128kbps → 每秒 16000 字节，320000 字节应该是 20 秒
        std::string data;
        for (int i = 0; i < 8; ++i)
            data += Mp3Frame(Mp3FrameHeader128(), 417);
        CHECK_NEAR(AudioDuration::Mp3DurationMs(data, 320000), 20000, 100);
    }
    {
        // ID3v2 要被跳过，而且它的长度不能算进音频字节里
        const size_t tag_body = 2048;
        std::string data = Id3v2Header(tag_body) + std::string(tag_body, 'x');
        const size_t audio_start = data.size();
        for (int i = 0; i < 8; ++i)
            data += Mp3Frame(Mp3FrameHeader128(), 417);
        CHECK_NEAR(AudioDuration::Mp3DurationMs(data, audio_start + 320000), 20000, 100);
    }
    {
        // 内嵌封面把第一个音频帧顶出了头部缓冲。
        // 这种情况必须明确返回 0，好让上层改走按偏移读的那条路——
        // 用户那首 ID3v2 有 640KB 的歌就是这么回事，进度条一直显示 "-:--"。
        const size_t tag_body = 640 * 1024;
        std::string head = Id3v2Header(tag_body) + std::string(4096, 'x');
        CHECK_EQ_INT(AudioDuration::Mp3DurationMs(head, 11000000), 0);

        // 把偏移单独告诉它，同一个文件就能算出来了
        std::string audio;
        for (int i = 0; i < 8; ++i)
            audio += Mp3Frame(Mp3FrameHeader128(), 417);
        const uint64_t audio_offset = 10 + tag_body;
        CHECK_NEAR(AudioDuration::Mp3DurationFromAudio(audio, audio_offset,
                                                       audio_offset + 320000), 20000, 100);
    }
    {
        // 全是垃圾字节：不能瞎猜出一个时长来
        CHECK_EQ_INT(AudioDuration::Mp3DurationMs(std::string(4096, 'z'), 4096), 0);
        CHECK_EQ_INT(AudioDuration::Mp3DurationMs(std::string(), 0), 0);
    }
    {
        // 孤立的一个同步字不算数：要求下一帧也对得上，
        // 否则封面数据里随便一个 0xFF 都会被当成音频起点
        std::string data = Mp3FrameHeader128() + std::string(64, 'x');
        CHECK_EQ_INT(AudioDuration::Mp3DurationMs(data, data.size()), 0);
    }
    {
        // 码率索引 0（自由码率）和 15（非法）都算不出时长
        std::string free_rate = Bytes({ 0xFF, 0xFB, 0x00, 0x44 }) + std::string(1024, '\0');
        CHECK_EQ_INT(AudioDuration::Mp3DurationMs(free_rate, free_rate.size()), 0);
        std::string bad_rate = Bytes({ 0xFF, 0xFB, 0xF0, 0x44 }) + std::string(1024, '\0');
        CHECK_EQ_INT(AudioDuration::Mp3DurationMs(bad_rate, bad_rate.size()), 0);
    }

    std::printf("AudioDuration WAV\n");
    {
        // 44100Hz 立体声 16bit → 每秒 176400 字节；352800 字节即 2 秒
        std::string wav = "RIFF" + LE32(0) + "WAVE";
        wav += "fmt " + LE32(16);
        wav += LE16(1) + LE16(2) + LE32(44100) + LE32(176400) + LE16(4) + LE16(16);
        wav += "data" + LE32(352800);
        CHECK_EQ_INT(AudioDuration::WavDurationMs(wav, wav.size() + 352800), 2000);

        // data 长度写成 0 的流式文件：按文件实际剩余长度补
        std::string streaming = "RIFF" + LE32(0) + "WAVE";
        streaming += "fmt " + LE32(16);
        streaming += LE16(1) + LE16(2) + LE32(44100) + LE32(176400) + LE16(4) + LE16(16);
        streaming += "data" + LE32(0);
        CHECK_EQ_INT(AudioDuration::WavDurationMs(streaming, streaming.size() + 176400), 1000);

        // fmt 之前插一个别的块，偏移推进要正确
        std::string with_junk = "RIFF" + LE32(0) + "WAVE";
        with_junk += "JUNK" + LE32(8) + std::string(8, '\0');
        with_junk += "fmt " + LE32(16);
        with_junk += LE16(1) + LE16(2) + LE32(44100) + LE32(176400) + LE16(4) + LE16(16);
        with_junk += "data" + LE32(176400);
        CHECK_EQ_INT(AudioDuration::WavDurationMs(with_junk, with_junk.size() + 176400), 1000);

        CHECK_EQ_INT(AudioDuration::WavDurationMs("NOTAWAVE________", 16), 0);
    }

    std::printf("AudioDuration Ogg\n");
    {
        // Vorbis：识别头里写着 44100Hz，末页 granule 441000 → 10 秒
        std::string ident = std::string(1, '\x01') + "vorbis";
        ident += LE32(0);                               // 版本
        ident += Bytes({ 2 });                          // 声道
        ident += LE32(44100);                           // 采样率
        const std::string head = OggPage(0, ident);
        const std::string tail = OggPage(441000, "audio");
        CHECK_EQ_INT(AudioDuration::OggDurationMs(head, tail), 10000);

        // 尾部有多个页时要取最后一个，不是第一个
        const std::string multi = OggPage(220500, "a") + OggPage(441000, "b");
        CHECK_EQ_INT(AudioDuration::OggDurationMs(head, multi), 10000);

        // Opus：granule 恒按 48kHz 计，还要减掉 pre-skip
        std::string opus = "OpusHead";
        opus += Bytes({ 1, 2 });                        // 版本、声道
        opus += LE16(312);                              // pre-skip
        opus += LE32(48000);
        const std::string opus_head = OggPage(0, opus);
        const std::string opus_tail = OggPage(480000 + 312, "audio");
        CHECK_EQ_INT(AudioDuration::OggDurationMs(opus_head, opus_tail), 10000);

        // 末页 granule 为 0：还没写完的文件，时长未知
        CHECK_EQ_INT(AudioDuration::OggDurationMs(head, OggPage(0, "x")), 0);
        CHECK_EQ_INT(AudioDuration::OggDurationMs("NOTOGG", tail), 0);
    }

    std::printf("AudioDuration FLAC\n");
    {
        CHECK_EQ_INT(AudioDuration::FlacDurationMs(FlacWithSamples(44100ULL * 123)), 123000);
        // 总采样数为 0 表示未知（边录边写的流）
        CHECK_EQ_INT(AudioDuration::FlacDurationMs(FlacWithSamples(0)), 0);
        CHECK_EQ_INT(AudioDuration::FlacDurationMs("NOTFLAC"), 0);
        // 第一个元数据块不是 STREAMINFO 的文件不合规，不该硬解
        std::string bad = FlacWithSamples(44100);
        bad[4] = static_cast<char>(0x04);
        CHECK_EQ_INT(AudioDuration::FlacDurationMs(bad), 0);
    }

    std::printf("AudioDuration 扩展名分派\n");
    {
        const std::string flac = FlacWithSamples(44100ULL * 60);
        CHECK_EQ_INT(AudioDuration::EstimateFromBuffers("flac", flac, "", flac.size()), 60000);
        // 不认识的扩展名不去猜
        CHECK_EQ_INT(AudioDuration::EstimateFromBuffers("mid", flac, "", flac.size()), 0);
        CHECK_EQ_INT(AudioDuration::EstimateFromBuffers("", flac, "", flac.size()), 0);
    }
}
