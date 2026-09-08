// TagWriter 的单元测试。
//
// 这块代码会重写用户的音乐文件，写错一个长度字段就是一首歌没了，
// 所以断言给得比别处密：不只验"能不能写进去"，还要验
//   - 原有的其它标签帧有没有被丢掉
//   - 长度字段是不是按该版本的规则编码的（v2.4 syncsafe / v2.3 普通大端）
//   - 重复嵌入会不会越写越大（旧的那份必须被换掉而不是叠加）
// 最后再用 AudioTag 把写出来的东西读一遍——两边是独立实现，能互相校验。
#include "TestFramework.h"
#include "../source/core/AudioTag.h"
#include "../source/core/TagWriter.h"

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

    std::string BE24(uint32_t value)
    {
        return Bytes({ static_cast<int>(value >> 16), static_cast<int>(value >> 8),
                       static_cast<int>(value) });
    }

    std::string LE32(uint32_t value)
    {
        return Bytes({ static_cast<int>(value), static_cast<int>(value >> 8),
                       static_cast<int>(value >> 16), static_cast<int>(value >> 24) });
    }

    std::string Syncsafe(uint32_t value)
    {
        return Bytes({ static_cast<int>((value >> 21) & 0x7F),
                       static_cast<int>((value >> 14) & 0x7F),
                       static_cast<int>((value >> 7) & 0x7F),
                       static_cast<int>(value & 0x7F) });
    }

    // 一个 ID3v2 帧。version >= 4 时长度字段是 syncsafe 的。
    std::string Id3Frame(const std::string& id, const std::string& body, int version)
    {
        std::string frame = id;
        frame += (version >= 4) ? Syncsafe(static_cast<uint32_t>(body.size()))
                                : BE32(static_cast<uint32_t>(body.size()));
        frame += std::string(2, '\0');
        frame += body;
        return frame;
    }

    // 一个文本帧的帧体：编码字节 + Latin-1 文本
    std::string TextBody(const std::string& text)
    {
        return std::string(1, '\0') + text;
    }

    std::string Id3Tag(const std::string& frames, int version)
    {
        std::string tag = "ID3";
        tag += Bytes({ version, 0, 0 });
        tag += Syncsafe(static_cast<uint32_t>(frames.size()));
        tag += frames;
        return tag;
    }

    // 在标签里找某个帧，返回帧体；找不到返回空串
    std::string FindFrame(const std::string& tag, const std::string& id, int version)
    {
        if (tag.size() < 10)
            return std::string();
        size_t offset = 10;
        while (offset + 10 <= tag.size())
        {
            const std::string current = tag.substr(offset, 4);
            uint32_t size = 0;
            for (int i = 0; i < 4; ++i)
            {
                const unsigned char byte = static_cast<unsigned char>(tag[offset + 4 + i]);
                size = (version >= 4) ? ((size << 7) | (byte & 0x7F)) : ((size << 8) | byte);
            }
            if (size == 0 || offset + 10 + size > tag.size())
                break;
            if (current == id)
                return tag.substr(offset + 10, size);
            offset += 10 + size;
        }
        return std::string();
    }

    std::string FlacBlock(int type, const std::string& body, bool last)
    {
        std::string out;
        out.push_back(static_cast<char>((last ? 0x80 : 0x00) | (type & 0x7F)));
        out += BE24(static_cast<uint32_t>(body.size()));
        out += body;
        return out;
    }

    std::string StreamInfoBody()
    {
        // 44100Hz / 立体声 / 16bit / 100 秒
        const uint64_t packed = (44100ULL << 44) | (1ULL << 41) | (15ULL << 36) | (44100ULL * 100);
        std::string body(10, '\0');
        for (int i = 7; i >= 0; --i)
            body.push_back(static_cast<char>((packed >> (i * 8)) & 0xFF));
        body += std::string(16, '\0');
        return body;
    }

    std::string VorbisCommentBody(std::initializer_list<const char*> entries)
    {
        const std::string vendor = "reference libFLAC";
        std::string body = LE32(static_cast<uint32_t>(vendor.size())) + vendor;
        body += LE32(static_cast<uint32_t>(entries.size()));
        for (const char* entry : entries)
        {
            const std::string text = entry;
            body += LE32(static_cast<uint32_t>(text.size())) + text;
        }
        return body;
    }

    // 数据里含 0x00 和 0xFF，能顺带验证长度字段没有被当成 C 字符串截断
    std::string FakeJpeg(size_t size)
    {
        std::string data;
        data += Bytes({ 0xFF, 0xD8, 0xFF, 0xE0 });
        while (data.size() < size)
            data.push_back(static_cast<char>(data.size() & 0xFF));
        return data;
    }
}

void RunTagWriteTests()
{
    std::printf("TagWriter 支持的格式\n");
    {
        CHECK(TagWriter::CanEmbed("sdmc:/music/a.mp3"));
        CHECK(TagWriter::CanEmbed("sdmc:/music/a.FLAC"));
        // Ogg/Opus 的注释头包在 Ogg 页里，改字段要重新分页并重算 CRC，不支持
        CHECK(!TagWriter::CanEmbed("sdmc:/music/a.ogg"));
        CHECK(!TagWriter::CanEmbed("sdmc:/music/a.opus"));
        CHECK(!TagWriter::CanEmbed("sdmc:/music/a.wav"));
    }

    std::printf("TagWriter ID3v2\n");
    {
        // 没有旧标签的裸 MP3：新建一个 v2.3 标签，音频从 0 开始
        AudioTag::Picture cover;
        cover.mime = "image/jpeg";
        cover.data = FakeJpeg(500);
        cover.type = 3;
        const std::string lyric = "[00:01.00]第一句\n[00:05.00]第二句\n";

        std::string tag;
        size_t audio_offset = 999;
        CHECK(TagWriter::BuildId3v2(std::string(1024, 'x'), &cover, &lyric, tag, audio_offset));
        CHECK_EQ_INT(static_cast<long long>(audio_offset), 0);
        CHECK_EQ(tag.substr(0, 3), "ID3");
        CHECK_EQ_INT(static_cast<unsigned char>(tag[3]), 3);

        const std::string apic = FindFrame(tag, "APIC", 3);
        CHECK(!apic.empty());
        // 编码(1) + "image/jpeg"(10) + 终止符(1) + 类型(1) + 空描述(1) = 14
        CHECK_EQ_INT(static_cast<long long>(apic.size()), 14 + 500);
        CHECK_EQ(apic.substr(1, 10), "image/jpeg");
        CHECK_EQ_INT(static_cast<unsigned char>(apic[12]), 3);

        const std::string uslt = FindFrame(tag, "USLT", 3);
        CHECK(!uslt.empty());
        CHECK_EQ_INT(static_cast<unsigned char>(uslt[0]), 3);        // UTF-8
        CHECK_EQ(uslt.substr(1, 3), "und");
        CHECK_EQ(uslt.substr(5), lyric);
    }
    {
        // 已有标签：其它帧必须原样留下，音频起点要跳过整个旧标签
        const std::string frames = Id3Frame("TIT2", TextBody("Title"), 3)
                                 + Id3Frame("TPE1", TextBody("Artist"), 3);
        const std::string old_tag = Id3Tag(frames, 3);

        AudioTag::Picture cover;
        cover.data = FakeJpeg(300);
        std::string tag;
        size_t audio_offset = 0;
        CHECK(TagWriter::BuildId3v2(old_tag, &cover, nullptr, tag, audio_offset));
        CHECK_EQ_INT(static_cast<long long>(audio_offset), static_cast<long long>(old_tag.size()));
        CHECK_EQ(FindFrame(tag, "TIT2", 3), TextBody("Title"));
        CHECK_EQ(FindFrame(tag, "TPE1", 3), TextBody("Artist"));
        CHECK(!FindFrame(tag, "APIC", 3).empty());
        // 只给了封面，不该凭空造一个歌词帧出来
        CHECK(FindFrame(tag, "USLT", 3).empty());
    }
    {
        // 重复嵌入：旧的 APIC 要被换掉而不是又加一个，否则每下一次文件就胖一圈
        AudioTag::Picture first;
        first.data = FakeJpeg(1000);
        std::string tag_once;
        size_t offset = 0;
        CHECK(TagWriter::BuildId3v2(std::string(), &first, nullptr, tag_once, offset));

        // 拿上一轮的产物当输入再来一次
        AudioTag::Picture second;
        second.data = FakeJpeg(1000);
        std::string tag_twice;
        CHECK(TagWriter::BuildId3v2(tag_once, &second, nullptr, tag_twice, offset));
        CHECK_EQ_INT(static_cast<long long>(tag_twice.size()),
                     static_cast<long long>(tag_once.size()));
    }
    {
        // v2.4 的输入要输出成 v2.4，且帧长度得用 syncsafe 编码。
        // 长度超过 0x80 才看得出区别，所以封面必须给大一点——
        // 用小图片测的话两种编码的结果是一样的，等于没测。
        const std::string old_tag = Id3Tag(Id3Frame("TIT2", TextBody("T"), 4), 4);
        AudioTag::Picture cover;
        cover.data = FakeJpeg(5000);

        std::string tag;
        size_t offset = 0;
        CHECK(TagWriter::BuildId3v2(old_tag, &cover, nullptr, tag, offset));
        CHECK_EQ_INT(static_cast<unsigned char>(tag[3]), 4);
        const std::string apic = FindFrame(tag, "APIC", 4);
        CHECK_EQ_INT(static_cast<long long>(apic.size()), 14 + 5000);
        // syncsafe 的每个字节最高位都必须是 0
        const std::string whole = tag;
        size_t apic_pos = whole.find("APIC");
        CHECK(apic_pos != std::string::npos);
        for (int i = 0; i < 4; ++i)
            CHECK((static_cast<unsigned char>(whole[apic_pos + 4 + i]) & 0x80) == 0);
    }
    {
        // v2.2 的帧头格式完全不同（3 字节 ID + 3 字节长度）。
        // 不去解析它，但也不能崩，而且音频起点必须算对——算错就等于把音频截断了。
        std::string old_tag = "ID3";
        old_tag += Bytes({ 2, 0, 0 });
        old_tag += Syncsafe(100);
        old_tag += std::string(100, 'z');

        AudioTag::Picture cover;
        cover.data = FakeJpeg(200);
        std::string tag;
        size_t offset = 0;
        CHECK(TagWriter::BuildId3v2(old_tag, &cover, nullptr, tag, offset));
        CHECK_EQ_INT(static_cast<long long>(offset), 110);
        CHECK_EQ_INT(static_cast<unsigned char>(tag[3]), 3);        // 输出降级成 v2.3
        CHECK(!FindFrame(tag, "APIC", 3).empty());
    }

    std::printf("TagWriter FLAC\n");
    {
        std::string flac = "fLaC";
        flac += FlacBlock(0, StreamInfoBody(), false);
        flac += FlacBlock(4, VorbisCommentBody({ "TITLE=歌名", "ARTIST=歌手" }), true);
        const size_t expected_audio_offset = flac.size();

        AudioTag::Picture cover;
        cover.mime = "image/png";
        cover.data = FakeJpeg(800);
        cover.type = 3;
        const std::string lyric = "[00:02.00]歌词\n";

        std::string metadata;
        size_t audio_offset = 0;
        CHECK(TagWriter::BuildFlacMetadata(flac, &cover, &lyric, metadata, audio_offset));
        CHECK_EQ_INT(static_cast<long long>(audio_offset),
                     static_cast<long long>(expected_audio_offset));
        CHECK_EQ(metadata.substr(0, 4), "fLaC");

        // 用 AudioTag 独立读一遍：两边是各自实现的，能互相校验
        AudioTag::Tag parsed;
        CHECK(AudioTag::ParseFlac(metadata, parsed));
        CHECK_EQ(parsed.title, "歌名");
        CHECK_EQ(parsed.artist, "歌手");

        AudioTag::Picture read_back;
        CHECK(AudioTag::ExtractFlacPicture(metadata, read_back));
        CHECK_EQ(read_back.mime, "image/png");
        CHECK_EQ_INT(static_cast<long long>(read_back.data.size()), 800);
        CHECK_EQ(read_back.data, cover.data);
    }
    {
        // 只写歌词：原有的封面块不该被顺手删掉
        std::string flac = "fLaC";
        flac += FlacBlock(0, StreamInfoBody(), false);
        flac += FlacBlock(6, std::string("EXISTINGPICTUREBLOCK"), true);

        const std::string lyric = "[00:00.00]x\n";
        std::string metadata;
        size_t offset = 0;
        CHECK(TagWriter::BuildFlacMetadata(flac, nullptr, &lyric, metadata, offset));
        CHECK(metadata.find("EXISTINGPICTUREBLOCK") != std::string::npos);
    }
    {
        // 重复嵌入：LYRICS 字段要被替换，不能越堆越多
        std::string flac = "fLaC";
        flac += FlacBlock(0, StreamInfoBody(), false);
        flac += FlacBlock(4, VorbisCommentBody({ "TITLE=T", "LYRICS=旧歌词" }), true);

        const std::string lyric = "新歌词";
        std::string metadata;
        size_t offset = 0;
        CHECK(TagWriter::BuildFlacMetadata(flac, nullptr, &lyric, metadata, offset));
        CHECK(metadata.find("旧歌词") == std::string::npos);
        CHECK(metadata.find("新歌词") != std::string::npos);
        // 同名的其它字段要留着
        CHECK(metadata.find("TITLE=T") != std::string::npos);

        // 再嵌一次，长度应该和上一次完全一样
        std::string again;
        CHECK(TagWriter::BuildFlacMetadata(metadata, nullptr, &lyric, again, offset));
        CHECK_EQ_INT(static_cast<long long>(again.size()),
                     static_cast<long long>(metadata.size()));
    }
    {
        // 最后一个块的 last 标志必须正好落在最后一个块上，
        // 早了会让解码器把后面的元数据当成音频帧，晚了就永远读不到音频
        std::string flac = "fLaC";
        flac += FlacBlock(0, StreamInfoBody(), true);

        AudioTag::Picture cover;
        cover.data = FakeJpeg(64);
        std::string metadata;
        size_t offset = 0;
        CHECK(TagWriter::BuildFlacMetadata(flac, &cover, nullptr, metadata, offset));

        size_t position = 4;
        int block_count = 0;
        bool saw_last = false;
        while (position + 4 <= metadata.size() && !saw_last)
        {
            const unsigned char flags = static_cast<unsigned char>(metadata[position]);
            saw_last = (flags & 0x80) != 0;
            uint32_t length = 0;
            for (int i = 1; i <= 3; ++i)
                length = (length << 8) | static_cast<unsigned char>(metadata[position + i]);
            position += 4 + length;
            ++block_count;
        }
        CHECK(saw_last);
        CHECK_EQ_INT(static_cast<long long>(position), static_cast<long long>(metadata.size()));
        CHECK_EQ_INT(block_count, 2);       // STREAMINFO + PICTURE
    }
    {
        // 元数据区没读全：必须明确失败，好让调用方多读一些再来，
        // 绝不能拿半截数据去重写文件
        std::string truncated = "fLaC";
        truncated += Bytes({ 0x00 });
        truncated += BE24(1000);
        truncated += std::string(10, '\0');     // 声称 1000 字节却只有 10

        std::string metadata;
        size_t offset = 0;
        AudioTag::Picture cover;
        cover.data = FakeJpeg(16);
        CHECK(!TagWriter::BuildFlacMetadata(truncated, &cover, nullptr, metadata, offset));
        CHECK(!TagWriter::BuildFlacMetadata("NOTFLAC", &cover, nullptr, metadata, offset));
    }
}
