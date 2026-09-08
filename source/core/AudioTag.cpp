#include "AudioTag.h"
#include "FileUtil.h"
#include "StringUtil.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    // ID3v2 的标签体可以很大（内嵌封面动辄几 MB）。文本帧一般排在封面前面，
    // 所以只读前面这么多字节就够用，避免扫一遍音乐库要读上百 MB。
    const size_t kMaxId3Bytes = 512 * 1024;
    // FLAC 的元数据块在文件最前面；Ogg 的 comment 包在第二个页里
    const size_t kMaxFlacBytes = 1024 * 1024;
    const size_t kMaxOggBytes = 256 * 1024;

    uint32_t ReadBE24(const std::string& d, size_t p)
    {
        return (static_cast<uint8_t>(d[p]) << 16) | (static_cast<uint8_t>(d[p + 1]) << 8)
             | static_cast<uint8_t>(d[p + 2]);
    }

    uint32_t ReadBE32(const std::string& d, size_t p)
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(d[p])) << 24)
             | (static_cast<uint32_t>(static_cast<uint8_t>(d[p + 1])) << 16)
             | (static_cast<uint32_t>(static_cast<uint8_t>(d[p + 2])) << 8)
             | static_cast<uint32_t>(static_cast<uint8_t>(d[p + 3]));
    }

    uint32_t ReadLE32(const std::string& d, size_t p)
    {
        return static_cast<uint32_t>(static_cast<uint8_t>(d[p]))
             | (static_cast<uint32_t>(static_cast<uint8_t>(d[p + 1])) << 8)
             | (static_cast<uint32_t>(static_cast<uint8_t>(d[p + 2])) << 16)
             | (static_cast<uint32_t>(static_cast<uint8_t>(d[p + 3])) << 24);
    }

    // ID3v2 的长度字段是"同步安全整数"：每字节只用低 7 位，
    // 这样长度里永远不会出现 0xFF 开头的字节被误认成 MPEG 帧同步头。
    uint32_t ReadSyncSafe(const std::string& d, size_t p)
    {
        return ((static_cast<uint32_t>(static_cast<uint8_t>(d[p])) & 0x7F) << 21)
             | ((static_cast<uint32_t>(static_cast<uint8_t>(d[p + 1])) & 0x7F) << 14)
             | ((static_cast<uint32_t>(static_cast<uint8_t>(d[p + 2])) & 0x7F) << 7)
             | (static_cast<uint32_t>(static_cast<uint8_t>(d[p + 3])) & 0x7F);
    }

    bool LooksLikeUtf8(const std::string& text)
    {
        size_t i = 0;
        bool has_multibyte = false;
        while (i < text.size())
        {
            unsigned char c = static_cast<unsigned char>(text[i]);
            size_t extra;
            if (c < 0x80)            extra = 0;
            else if ((c & 0xE0) == 0xC0) extra = 1;
            else if ((c & 0xF0) == 0xE0) extra = 2;
            else if ((c & 0xF8) == 0xF0) extra = 3;
            else return false;

            if (extra > 0)
            {
                has_multibyte = true;
                if (i + extra >= text.size())
                    return false;
                for (size_t k = 1; k <= extra; ++k)
                {
                    if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
                        return false;
                }
            }
            i += extra + 1;
        }
        return has_multibyte;
    }

    std::string Latin1ToUtf8(const std::string& src)
    {
        std::string out;
        out.reserve(src.size());
        for (unsigned char c : src)
        {
            if (c < 0x80)
            {
                out += static_cast<char>(c);
            }
            else
            {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return out;
    }

    std::string Utf16ToUtf8Bytes(const std::string& src, bool big_endian, bool skip_bom)
    {
        size_t start = 0;
        if (skip_bom && src.size() >= 2)
        {
            unsigned char b0 = static_cast<unsigned char>(src[0]);
            unsigned char b1 = static_cast<unsigned char>(src[1]);
            if (b0 == 0xFF && b1 == 0xFE) { big_endian = false; start = 2; }
            else if (b0 == 0xFE && b1 == 0xFF) { big_endian = true; start = 2; }
        }

        std::u16string u16;
        u16.reserve((src.size() - start) / 2);
        for (size_t i = start; i + 1 < src.size(); i += 2)
        {
            unsigned char a = static_cast<unsigned char>(src[i]);
            unsigned char b = static_cast<unsigned char>(src[i + 1]);
            char16_t unit = big_endian ? static_cast<char16_t>((a << 8) | b)
                                       : static_cast<char16_t>((b << 8) | a);
            if (unit == 0)
                break;                          // 提前结束的空字符
            u16 += unit;
        }
        return StringUtil::Utf16ToUtf8(u16);
    }

    // ID3v2 文本帧的第一个字节是编码标志
    std::string DecodeId3Text(const std::string& raw)
    {
        if (raw.empty())
            return std::string();

        unsigned char encoding = static_cast<unsigned char>(raw[0]);
        std::string body = raw.substr(1);
        // 去掉尾部填充的空字符
        while (!body.empty() && body.back() == '\0')
            body.pop_back();

        std::string text;
        switch (encoding)
        {
        case 1:  text = Utf16ToUtf8Bytes(body, false, true); break;   // 带 BOM 的 UTF-16
        case 2:  text = Utf16ToUtf8Bytes(body, true, false); break;   // 无 BOM 的 UTF-16BE
        case 3:  text = body; break;                                  // UTF-8
        default:
            // 标准规定编码 0 是 ISO-8859-1，但现实中很多打标签的软件直接往里塞 UTF-8。
            // 内容本身是合法 UTF-8 且含多字节字符时按 UTF-8 处理，比机械照标准更实用。
            // （注：也有软件塞 GBK，那种情况这里认不出来，会得到乱码；
            //   要正确处理得带一张 GBK 映射表，代价不值得。）
            text = LooksLikeUtf8(body) ? body : Latin1ToUtf8(body);
            break;
        }
        return StringUtil::Trimmed(text);
    }

    void AssignIfEmpty(std::string& target, const std::string& value)
    {
        if (target.empty() && !value.empty())
            target = value;
    }

    int ParseLeadingInt(const std::string& text)
    {
        // "3/12" 这种取前面的数字；"2003-05-01" 取年份
        return std::atoi(text.c_str());
    }

}

namespace AudioTag
{

bool ParseId3v2(const std::string& data, Tag& out)
{
    if (data.size() < 10 || data.compare(0, 3, "ID3") != 0)
        return false;

    int major = static_cast<unsigned char>(data[3]);
    if (major < 2 || major > 4)
        return false;                       // 未来版本，结构未知
    unsigned char flags = static_cast<unsigned char>(data[5]);
    uint32_t tag_size = ReadSyncSafe(data, 6);

    size_t pos = 10;
    // 扩展头（v2.3/v2.4 的 0x40 标志）直接跳过，里面没有我们要的东西
    if (major >= 3 && (flags & 0x40) != 0 && pos + 4 <= data.size())
    {
        uint32_t ext_size = (major == 4) ? ReadSyncSafe(data, pos) : ReadBE32(data, pos);
        pos += (major == 4) ? ext_size : ext_size + 4;
    }

    size_t tag_end = 10 + tag_size;
    if (tag_end > data.size())
        tag_end = data.size();              // 只读了开头一段，按实际长度截断

    const size_t id_len = (major == 2) ? 3 : 4;
    const size_t header_len = (major == 2) ? 6 : 10;

    bool found = false;
    while (pos + header_len <= tag_end)
    {
        std::string id = data.substr(pos, id_len);
        if (id[0] == '\0')
            break;                          // 进入填充区

        uint32_t frame_size;
        if (major == 2)
            frame_size = ReadBE24(data, pos + 3);
        else if (major == 4)
            frame_size = ReadSyncSafe(data, pos + 4);
        else
            frame_size = ReadBE32(data, pos + 4);

        pos += header_len;
        if (frame_size == 0 || pos + frame_size > tag_end)
            break;

        std::string body = data.substr(pos, frame_size);
        pos += frame_size;

        // v2.2 用 3 字母帧 ID，v2.3/v2.4 用 4 字母
        if (id == "TIT2" || id == "TT2")
        {
            AssignIfEmpty(out.title, DecodeId3Text(body));
            found = true;
        }
        else if (id == "TPE1" || id == "TP1")
        {
            AssignIfEmpty(out.artist, DecodeId3Text(body));
            found = true;
        }
        else if (id == "TALB" || id == "TAL")
        {
            AssignIfEmpty(out.album, DecodeId3Text(body));
            found = true;
        }
        else if (id == "TCON" || id == "TCO")
        {
            AssignIfEmpty(out.genre, DecodeId3Text(body));
        }
        else if (id == "TRCK" || id == "TRK")
        {
            if (out.track == 0)
                out.track = ParseLeadingInt(DecodeId3Text(body));
        }
        else if (id == "TYER" || id == "TYE" || id == "TDRC")
        {
            if (out.year == 0)
                out.year = ParseLeadingInt(DecodeId3Text(body));
        }
    }
    return found;
}

bool ParseId3v1(const std::string& tail, Tag& out)
{
    if (tail.size() < 128)
        return false;
    size_t base = tail.size() - 128;
    if (tail.compare(base, 3, "TAG") != 0)
        return false;

    // 字段是定长的，用空格和空字符填充，取出来要去掉尾部填充
    auto field = [&](size_t offset, size_t length) {
        std::string s = tail.substr(base + offset, length);
        while (!s.empty() && (s.back() == '\0' || s.back() == ' '))
            s.pop_back();
        return StringUtil::Trimmed(LooksLikeUtf8(s) ? s : Latin1ToUtf8(s));
    };

    AssignIfEmpty(out.title, field(3, 30));
    AssignIfEmpty(out.artist, field(33, 30));
    AssignIfEmpty(out.album, field(63, 30));
    if (out.year == 0)
        out.year = ParseLeadingInt(field(93, 4));
    // ID3v1.1 把第 126 字节用作音轨号（前提是第 125 字节为 0）
    if (out.track == 0 && tail[base + 125] == '\0')
        out.track = static_cast<unsigned char>(tail[base + 126]);
    return true;
}

bool ParseVorbisComment(const std::string& data, size_t offset, Tag& out)
{
    if (offset + 4 > data.size())
        return false;
    uint32_t vendor_len = ReadLE32(data, offset);
    offset += 4;
    if (offset + vendor_len + 4 > data.size())
        return false;
    offset += vendor_len;

    uint32_t count = ReadLE32(data, offset);
    offset += 4;
    // 条目数来自文件，必须限幅：损坏的文件可能给出一个巨大的数字
    if (count > 4096)
        return false;

    bool found = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (offset + 4 > data.size())
            break;
        uint32_t len = ReadLE32(data, offset);
        offset += 4;
        if (offset + len > data.size())
            break;
        std::string entry = data.substr(offset, len);
        offset += len;

        size_t equals = entry.find('=');
        if (equals == std::string::npos)
            continue;
        std::string key = StringUtil::ToLower(entry.substr(0, equals));
        std::string value = StringUtil::Trimmed(entry.substr(equals + 1));
        if (value.empty())
            continue;

        // Vorbis comment 规定内容就是 UTF-8，不需要猜编码
        if (key == "title")            { AssignIfEmpty(out.title, value);  found = true; }
        else if (key == "artist")      { AssignIfEmpty(out.artist, value); found = true; }
        else if (key == "album")       { AssignIfEmpty(out.album, value);  found = true; }
        else if (key == "genre")       { AssignIfEmpty(out.genre, value); }
        else if (key == "tracknumber") { if (out.track == 0) out.track = ParseLeadingInt(value); }
        else if (key == "date")        { if (out.year == 0)  out.year = ParseLeadingInt(value); }
    }
    return found;
}

bool ParseFlac(const std::string& data, Tag& out)
{
    if (data.size() < 4 || data.compare(0, 4, "fLaC") != 0)
        return false;

    size_t pos = 4;
    while (pos + 4 <= data.size())
    {
        unsigned char header = static_cast<unsigned char>(data[pos]);
        bool last = (header & 0x80) != 0;
        int type = header & 0x7F;
        uint32_t length = ReadBE24(data, pos + 1);
        pos += 4;

        if (type == 4)                      // VORBIS_COMMENT
            return ParseVorbisComment(data, pos, out);

        if (last)
            break;
        pos += length;
    }
    return false;
}

bool ParseOgg(const std::string& data, Tag& out)
{
    if (data.size() < 4 || data.compare(0, 4, "OggS") != 0)
        return false;

    // 严格做法是按 Ogg 的页和段表把第二个数据包重组出来。这里用定位标记的
    // 简化办法：comment 头通常完整地待在一个页里，够用了。
    // 代价是极少数被跨页切分的文件读不到标签 —— 那只是显示回退到文件名，
    // 不会出错。
    size_t pos = data.find("\x03vorbis", 0, 7);
    if (pos != std::string::npos)
        return ParseVorbisComment(data, pos + 7, out);

    pos = data.find("OpusTags", 0, 8);
    if (pos != std::string::npos)
        return ParseVorbisComment(data, pos + 8, out);

    return false;
}

namespace
{
    // 先读一小块试着解析，不够再读大块。
    //
    // 扫描一个几百首的音乐库时，每首都读几百 KB 是不可接受的（SD 卡上要好几十秒）。
    // 绝大多数文件的标题/艺术家就在最前面几 KB 里，小块读一次就够；
    // 只有内嵌封面排在文本帧前面的少数文件才需要读大块。
    bool ReadProgressive(const std::string& path, AudioTag::Tag& out, size_t small_size,
                         size_t large_size, bool (*parse)(const std::string&, AudioTag::Tag&))
    {
        std::string data;
        if (FileUtil::ReadHead(path, small_size, data) && parse(data, out)
            && !out.title.empty() && !out.artist.empty())
        {
            return true;
        }
        // 小块没读全，清掉可能只填了一半的结果重来
        out = AudioTag::Tag();
        if (data.size() < small_size)
            return false;                   // 文件本来就没那么大，读大块也是同样的内容
        if (!FileUtil::ReadHead(path, large_size, data))
            return false;
        return parse(data, out);
    }

    const size_t kFirstChunk = 32 * 1024;
}

bool Read(const std::string& file_path, Tag& out)
{
    out = Tag();

    std::string ext = FileUtil::GetExtension(file_path);

    if (ext == "flac")
        return ReadProgressive(file_path, out, kFirstChunk, kMaxFlacBytes, ParseFlac);
    if (ext == "ogg" || ext == "oga" || ext == "opus")
        return ReadProgressive(file_path, out, kFirstChunk, kMaxOggBytes, ParseOgg);

    if (ext == "mp3")
    {
        bool found = ReadProgressive(file_path, out, kFirstChunk, kMaxId3Bytes, ParseId3v2);
        // ID3v2 缺字段时用末尾的 ID3v1 补齐（AssignIfEmpty 保证不会覆盖已有值）
        if (out.title.empty() || out.artist.empty())
        {
            std::string tail;
            if (FileUtil::ReadTail(file_path, 128, tail))
                found = ParseId3v1(tail, out) || found;
        }
        return found && !out.IsEmpty();
    }

    // 其余格式（wav / mod 系列 / mid）不带通用标签
    return false;
}

}   // namespace AudioTag
