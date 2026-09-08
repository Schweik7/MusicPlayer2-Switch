// 音频标签解析的主机端测试。
//
// 标签解析全是偏移量和长度计算，写错一个字节就会读出乱码或越界，
// 而在真机上只表现为"曲目名不对"，极难定位。所以这里用内存里手工拼出来的
// 标签数据逐种格式验证，不依赖任何真实音频文件。

#include "TestFramework.h"

#include "../source/core/AudioTag.h"
#include "../source/core/FileUtil.h"

#include <cstdio>
#include <string>

namespace
{
    void AppendBE32(std::string& out, uint32_t value)
    {
        out += static_cast<char>((value >> 24) & 0xFF);
        out += static_cast<char>((value >> 16) & 0xFF);
        out += static_cast<char>((value >> 8) & 0xFF);
        out += static_cast<char>(value & 0xFF);
    }

    void AppendLE32(std::string& out, uint32_t value)
    {
        out += static_cast<char>(value & 0xFF);
        out += static_cast<char>((value >> 8) & 0xFF);
        out += static_cast<char>((value >> 16) & 0xFF);
        out += static_cast<char>((value >> 24) & 0xFF);
    }

    void AppendSyncSafe(std::string& out, uint32_t value)
    {
        out += static_cast<char>((value >> 21) & 0x7F);
        out += static_cast<char>((value >> 14) & 0x7F);
        out += static_cast<char>((value >> 7) & 0x7F);
        out += static_cast<char>(value & 0x7F);
    }

    // encoding: 0=Latin-1 1=UTF-16+BOM 2=UTF-16BE 3=UTF-8
    std::string MakeTextFrame(const std::string& id, char encoding, const std::string& payload)
    {
        std::string body;
        body += encoding;
        body += payload;

        std::string frame = id;
        AppendBE32(frame, static_cast<uint32_t>(body.size()));
        frame += '\0';                  // flags
        frame += '\0';
        frame += body;
        return frame;
    }

    std::string MakeId3v2_3(const std::string& frames)
    {
        std::string tag = "ID3";
        tag += static_cast<char>(3);    // 主版本
        tag += static_cast<char>(0);    // 修订号
        tag += static_cast<char>(0);    // flags
        AppendSyncSafe(tag, static_cast<uint32_t>(frames.size()));
        tag += frames;
        return tag;
    }

    // 把 UTF-8 转成带 BOM 的小端 UTF-16 字节串（只覆盖 BMP，测试够用）
    std::string Utf8ToUtf16LeWithBom(const std::string& utf8)
    {
        std::string out;
        out += static_cast<char>(0xFF);
        out += static_cast<char>(0xFE);
        size_t i = 0;
        while (i < utf8.size())
        {
            unsigned char c = static_cast<unsigned char>(utf8[i]);
            uint32_t cp;
            size_t len;
            if (c < 0x80)               { cp = c; len = 1; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
            else                         { cp = c & 0x07; len = 4; }
            for (size_t k = 1; k < len && i + k < utf8.size(); ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
            i += len;
            out += static_cast<char>(cp & 0xFF);
            out += static_cast<char>((cp >> 8) & 0xFF);
        }
        return out;
    }

    std::string MakeVorbisComment(const std::vector<std::string>& entries)
    {
        std::string out;
        const std::string vendor = "test-vendor";
        AppendLE32(out, static_cast<uint32_t>(vendor.size()));
        out += vendor;
        AppendLE32(out, static_cast<uint32_t>(entries.size()));
        for (const std::string& entry : entries)
        {
            AppendLE32(out, static_cast<uint32_t>(entry.size()));
            out += entry;
        }
        return out;
    }

    std::string MakeFlac(const std::string& comment_payload, bool put_picture_first)
    {
        std::string out = "fLaC";

        auto append_block = [&](int type, const std::string& payload, bool last) {
            out += static_cast<char>((last ? 0x80 : 0x00) | type);
            uint32_t len = static_cast<uint32_t>(payload.size());
            out += static_cast<char>((len >> 16) & 0xFF);
            out += static_cast<char>((len >> 8) & 0xFF);
            out += static_cast<char>(len & 0xFF);
            out += payload;
        };

        append_block(0, std::string(34, '\0'), false);           // STREAMINFO
        if (put_picture_first)
        {
            // 内嵌封面排在注释块前面：解析器必须靠长度正确跳过它
            append_block(6, std::string(5000, '\xAB'), false);   // PICTURE
        }
        append_block(4, comment_payload, true);                  // VORBIS_COMMENT
        return out;
    }
}

static void TestId3v2()
{
    std::puts("AudioTag / ID3v2");

    // ---- v2.3，UTF-8 编码的中文 ----
    {
        std::string frames;
        frames += MakeTextFrame("TIT2", 3, u8"晴天");
        frames += MakeTextFrame("TPE1", 3, u8"周杰伦");
        frames += MakeTextFrame("TALB", 3, u8"叶惠美");
        frames += MakeTextFrame("TRCK", 3, "3/12");
        frames += MakeTextFrame("TYER", 3, "2003");
        frames += MakeTextFrame("TCON", 3, "Pop");

        AudioTag::Tag tag;
        CHECK(AudioTag::ParseId3v2(MakeId3v2_3(frames), tag));
        CHECK_EQ(tag.title, u8"晴天");
        CHECK_EQ(tag.artist, u8"周杰伦");
        CHECK_EQ(tag.album, u8"叶惠美");
        CHECK_EQ(tag.genre, "Pop");
        CHECK_EQ_INT(tag.track, 3);         // "3/12" 取斜杠前面的
        CHECK_EQ_INT(tag.year, 2003);
    }

    // ---- v2.3，UTF-16 带 BOM（Windows 上的打标签软件最常用这种）----
    {
        std::string frames;
        frames += MakeTextFrame("TIT2", 1, Utf8ToUtf16LeWithBom(u8"七里香"));
        frames += MakeTextFrame("TPE1", 1, Utf8ToUtf16LeWithBom(u8"周杰伦"));

        AudioTag::Tag tag;
        CHECK(AudioTag::ParseId3v2(MakeId3v2_3(frames), tag));
        CHECK_EQ(tag.title, u8"七里香");
        CHECK_EQ(tag.artist, u8"周杰伦");
    }

    // ---- 编码标记为 Latin-1 但内容其实是 UTF-8：现实中很常见，必须认出来 ----
    {
        std::string frames = MakeTextFrame("TIT2", 0, u8"稻香");
        AudioTag::Tag tag;
        CHECK(AudioTag::ParseId3v2(MakeId3v2_3(frames), tag));
        CHECK_EQ(tag.title, u8"稻香");
    }

    // ---- 真正的 Latin-1 内容要按 Latin-1 转换 ----
    {
        std::string latin1;
        latin1 += 'C';
        latin1 += static_cast<char>(0xF4);      // ô
        latin1 += "me";
        std::string frames = MakeTextFrame("TIT2", 0, latin1);
        AudioTag::Tag tag;
        CHECK(AudioTag::ParseId3v2(MakeId3v2_3(frames), tag));
        CHECK_EQ(tag.title, u8"Côme");
    }

    // ---- v2.4：帧长度是同步安全整数，用 v2.3 的读法会读错 ----
    {
        std::string body;
        body += static_cast<char>(3);
        body += u8"告白气球";

        std::string frame = "TIT2";
        AppendSyncSafe(frame, static_cast<uint32_t>(body.size()));
        frame += '\0';
        frame += '\0';
        frame += body;

        std::string tag_data = "ID3";
        tag_data += static_cast<char>(4);
        tag_data += static_cast<char>(0);
        tag_data += static_cast<char>(0);
        AppendSyncSafe(tag_data, static_cast<uint32_t>(frame.size()));
        tag_data += frame;

        AudioTag::Tag tag;
        CHECK(AudioTag::ParseId3v2(tag_data, tag));
        CHECK_EQ(tag.title, u8"告白气球");
    }

    // ---- v2.2：3 字母帧 ID + 3 字节长度 ----
    {
        std::string body;
        body += static_cast<char>(3);
        body += u8"双截棍";

        std::string frame = "TT2";
        uint32_t len = static_cast<uint32_t>(body.size());
        frame += static_cast<char>((len >> 16) & 0xFF);
        frame += static_cast<char>((len >> 8) & 0xFF);
        frame += static_cast<char>(len & 0xFF);
        frame += body;

        std::string tag_data = "ID3";
        tag_data += static_cast<char>(2);
        tag_data += static_cast<char>(0);
        tag_data += static_cast<char>(0);
        AppendSyncSafe(tag_data, static_cast<uint32_t>(frame.size()));
        tag_data += frame;

        AudioTag::Tag tag;
        CHECK(AudioTag::ParseId3v2(tag_data, tag));
        CHECK_EQ(tag.title, u8"双截棍");
    }

    // ---- 损坏与边界输入：必须安全返回 false，不能越界 ----
    {
        AudioTag::Tag tag;
        CHECK(!AudioTag::ParseId3v2("", tag));
        CHECK(!AudioTag::ParseId3v2("ID3", tag));
        CHECK(!AudioTag::ParseId3v2("NOTATAG___", tag));
        // 声称的帧长度超出实际数据，不能读越界
        std::string frame = "TIT2";
        AppendBE32(frame, 0x00FFFFFF);
        frame += '\0';
        frame += '\0';
        frame += "short";
        CHECK(!AudioTag::ParseId3v2(MakeId3v2_3(frame), tag));
        // 版本号不认识
        std::string bad_version = MakeId3v2_3(MakeTextFrame("TIT2", 3, "x"));
        bad_version[3] = static_cast<char>(9);
        CHECK(!AudioTag::ParseId3v2(bad_version, tag));
    }
}

static void TestId3v1()
{
    std::puts("AudioTag / ID3v1");

    std::string tail(128, '\0');
    tail.replace(0, 3, "TAG");
    tail.replace(3, 5, "Title");
    tail.replace(33, 6, "Artist");
    tail.replace(63, 5, "Album");
    tail.replace(93, 4, "1999");
    tail[125] = '\0';
    tail[126] = static_cast<char>(7);        // ID3v1.1 的音轨号

    AudioTag::Tag tag;
    CHECK(AudioTag::ParseId3v1(tail, tag));
    CHECK_EQ(tag.title, "Title");
    CHECK_EQ(tag.artist, "Artist");
    CHECK_EQ(tag.album, "Album");
    CHECK_EQ_INT(tag.year, 1999);
    CHECK_EQ_INT(tag.track, 7);

    // 没有 TAG 标记
    AudioTag::Tag empty;
    CHECK(!AudioTag::ParseId3v1(std::string(128, '\0'), empty));
    CHECK(!AudioTag::ParseId3v1("too short", empty));
}

static void TestFlacAndOgg()
{
    std::puts("AudioTag / FLAC + Ogg");

    std::vector<std::string> entries{
        std::string(u8"TITLE=夜曲"),
        std::string(u8"ARTIST=周杰伦"),
        std::string(u8"ALBUM=十一月的萧邦"),
        "TRACKNUMBER=2",
        "DATE=2005-11-01",
        "GENRE=Pop",
    };
    std::string comment = MakeVorbisComment(entries);

    // ---- FLAC，注释块紧跟 STREAMINFO ----
    {
        AudioTag::Tag tag;
        CHECK(AudioTag::ParseFlac(MakeFlac(comment, false), tag));
        CHECK_EQ(tag.title, u8"夜曲");
        CHECK_EQ(tag.artist, u8"周杰伦");
        CHECK_EQ(tag.album, u8"十一月的萧邦");
        CHECK_EQ_INT(tag.track, 2);
        CHECK_EQ_INT(tag.year, 2005);       // "2005-11-01" 取年份
    }

    // ---- FLAC，注释块前面隔着 5000 字节的封面块：必须靠长度正确跳过 ----
    {
        AudioTag::Tag tag;
        CHECK(AudioTag::ParseFlac(MakeFlac(comment, true), tag));
        CHECK_EQ(tag.title, u8"夜曲");
    }

    // ---- 键名大小写不敏感 ----
    {
        std::string mixed = MakeVorbisComment({ std::string(u8"Title=菊花台"),
                                                std::string(u8"aRTIST=周杰伦") });
        AudioTag::Tag tag;
        CHECK(AudioTag::ParseVorbisComment(mixed, 0, tag));
        CHECK_EQ(tag.title, u8"菊花台");
        CHECK_EQ(tag.artist, u8"周杰伦");
    }

    // ---- Ogg Vorbis ----
    {
        std::string ogg = "OggS";
        ogg += std::string(60, '\0');
        ogg += "\x03";
        ogg += "vorbis";
        ogg += comment;
        AudioTag::Tag tag;
        CHECK(AudioTag::ParseOgg(ogg, tag));
        CHECK_EQ(tag.title, u8"夜曲");
    }

    // ---- Opus ----
    {
        std::string opus = "OggS";
        opus += std::string(60, '\0');
        opus += "OpusTags";
        opus += comment;
        AudioTag::Tag tag;
        CHECK(AudioTag::ParseOgg(opus, tag));
        CHECK_EQ(tag.artist, u8"周杰伦");
    }

    // ---- 损坏输入 ----
    {
        AudioTag::Tag tag;
        CHECK(!AudioTag::ParseFlac("", tag));
        CHECK(!AudioTag::ParseFlac("NotFlacData", tag));
        CHECK(!AudioTag::ParseOgg("NotOgg", tag));
        // 条目数字段被写成一个巨大的值，不能因此读越界或死循环
        std::string bogus;
        AppendLE32(bogus, 0);               // vendor 长度
        AppendLE32(bogus, 0xFFFFFFFF);      // 条目数
        CHECK(!AudioTag::ParseVorbisComment(bogus, 0, tag));
        // 条目长度超出剩余数据
        std::string truncated;
        AppendLE32(truncated, 0);
        AppendLE32(truncated, 1);
        AppendLE32(truncated, 9999);
        truncated += "TITLE=x";
        AudioTag::Tag t2;
        CHECK(!AudioTag::ParseVorbisComment(truncated, 0, t2));
    }
}

static void TestReadFromDisk()
{
    std::puts("AudioTag / 读文件");

    const std::string dir = "test_tmp_tag";
    TestFramework::RemoveTestDir(dir);
    CHECK(FileUtil::CreateDirRecursive(dir));

    // 文件名是拼音（Switch 上中文文件名存不了，只能这样传），
    // 但标签里是中文——这正是这个模块要解决的场景
    const std::string mp3_path = dir + "/zhou_jie_lun_-_qing_tian.mp3";
    std::string frames;
    frames += MakeTextFrame("TIT2", 3, u8"晴天");
    frames += MakeTextFrame("TPE1", 3, u8"周杰伦");
    std::string mp3 = MakeId3v2_3(frames);
    mp3 += std::string(4096, '\xFF');       // 假装是音频数据
    CHECK(FileUtil::WriteAll(mp3_path, mp3));

    AudioTag::Tag tag;
    CHECK(AudioTag::Read(mp3_path, tag));
    CHECK_EQ(tag.title, u8"晴天");
    CHECK_EQ(tag.artist, u8"周杰伦");

    // FLAC
    const std::string flac_path = dir + "/batta_-_chase.flac";
    std::string flac = MakeFlac(MakeVorbisComment({ std::string(u8"TITLE=追逐"),
                                                    std::string(u8"ARTIST=batta") }), false);
    CHECK(FileUtil::WriteAll(flac_path, flac));
    AudioTag::Tag flac_tag;
    CHECK(AudioTag::Read(flac_path, flac_tag));
    CHECK_EQ(flac_tag.title, u8"追逐");

    // 不带标签的格式：应当干净地返回 false，而不是报错或读出垃圾
    const std::string wav_path = dir + "/beep.wav";
    CHECK(FileUtil::WriteAll(wav_path, "RIFF....WAVEfmt "));
    AudioTag::Tag wav_tag;
    CHECK(!AudioTag::Read(wav_path, wav_tag));
    CHECK(wav_tag.IsEmpty());

    // 文件不存在
    AudioTag::Tag missing;
    CHECK(!AudioTag::Read(dir + "/nope.mp3", missing));

    // 只有音频数据、没有标签的 mp3
    const std::string bare_path = dir + "/bare.mp3";
    CHECK(FileUtil::WriteAll(bare_path, std::string(2048, '\xFF')));
    AudioTag::Tag bare;
    CHECK(!AudioTag::Read(bare_path, bare));

    TestFramework::RemoveTestDir(dir);
}

void RunTagTests()
{
    TestId3v2();
    TestId3v1();
    TestFlacAndOgg();
    TestReadFromDisk();
}
