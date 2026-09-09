#include "TagWriter.h"
#include "FileUtil.h"
#include "StringUtil.h"

#include <algorithm>
#include <cstdio>
#include <vector>
#include "Lang.h"

namespace
{
    inline unsigned char Byte(const std::string& data, size_t index)
    {
        return static_cast<unsigned char>(data[index]);
    }

    void AppendBE32(std::string& out, uint32_t value)
    {
        out.push_back(static_cast<char>((value >> 24) & 0xFF));
        out.push_back(static_cast<char>((value >> 16) & 0xFF));
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
        out.push_back(static_cast<char>(value & 0xFF));
    }

    void AppendBE24(std::string& out, uint32_t value)
    {
        out.push_back(static_cast<char>((value >> 16) & 0xFF));
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
        out.push_back(static_cast<char>(value & 0xFF));
    }

    void AppendLE32(std::string& out, uint32_t value)
    {
        out.push_back(static_cast<char>(value & 0xFF));
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
        out.push_back(static_cast<char>((value >> 16) & 0xFF));
        out.push_back(static_cast<char>((value >> 24) & 0xFF));
    }

    uint32_t ReadBE32(const std::string& data, size_t offset)
    {
        return (static_cast<uint32_t>(Byte(data, offset)) << 24)
             | (static_cast<uint32_t>(Byte(data, offset + 1)) << 16)
             | (static_cast<uint32_t>(Byte(data, offset + 2)) << 8)
             |  static_cast<uint32_t>(Byte(data, offset + 3));
    }

    uint32_t ReadBE24(const std::string& data, size_t offset)
    {
        return (static_cast<uint32_t>(Byte(data, offset)) << 16)
             | (static_cast<uint32_t>(Byte(data, offset + 1)) << 8)
             |  static_cast<uint32_t>(Byte(data, offset + 2));
    }

    uint32_t ReadLE32(const std::string& data, size_t offset)
    {
        return  static_cast<uint32_t>(Byte(data, offset))
             | (static_cast<uint32_t>(Byte(data, offset + 1)) << 8)
             | (static_cast<uint32_t>(Byte(data, offset + 2)) << 16)
             | (static_cast<uint32_t>(Byte(data, offset + 3)) << 24);
    }

    // ---- ID3v2 ----

    uint32_t ReadSyncsafe(const std::string& data, size_t offset)
    {
        return (static_cast<uint32_t>(Byte(data, offset) & 0x7F) << 21)
             | (static_cast<uint32_t>(Byte(data, offset + 1) & 0x7F) << 14)
             | (static_cast<uint32_t>(Byte(data, offset + 2) & 0x7F) << 7)
             |  static_cast<uint32_t>(Byte(data, offset + 3) & 0x7F);
    }

    void AppendSyncsafe(std::string& out, uint32_t value)
    {
        out.push_back(static_cast<char>((value >> 21) & 0x7F));
        out.push_back(static_cast<char>((value >> 14) & 0x7F));
        out.push_back(static_cast<char>((value >> 7) & 0x7F));
        out.push_back(static_cast<char>(value & 0x7F));
    }

    // 帧长度字段：v2.4 是 syncsafe，v2.3 是普通的大端整数。
    // 这两者只在长度超过 0x80 时才有区别，所以搞错了不会立刻炸，
    // 而是在遇到大封面的文件时才出问题——正是最难查的那种。
    void AppendFrameSize(std::string& out, uint32_t value, int version)
    {
        if (version >= 4)
            AppendSyncsafe(out, value);
        else
            AppendBE32(out, value);
    }

    uint32_t ReadFrameSize(const std::string& data, size_t offset, int version)
    {
        return (version >= 4) ? ReadSyncsafe(data, offset) : ReadBE32(data, offset);
    }

    std::string MakeId3Frame(const char* id, const std::string& body, int version)
    {
        std::string frame(id, 4);
        AppendFrameSize(frame, static_cast<uint32_t>(body.size()), version);
        frame += std::string(2, '\0');      // 帧标志位，两个字节全 0
        frame += body;
        return frame;
    }

    // APIC：编码(1) + MIME(latin1, 以 0 结尾) + 图片类型(1) + 描述(以 0 结尾) + 图片数据
    std::string MakeApicBody(const AudioTag::Picture& picture)
    {
        std::string body;
        body.push_back('\0');               // 编码 0 = ISO-8859-1，描述留空所以用哪种都行
        body += picture.mime.empty() ? std::string("image/jpeg") : picture.mime;
        body.push_back('\0');
        body.push_back(static_cast<char>(picture.type == 0 ? 3 : picture.type));
        body.push_back('\0');               // 空描述
        body += picture.data;
        return body;
    }

    // USLT：编码(1) + 语言(3) + 描述(以 0 结尾) + 歌词。
    // 用 UTF-8（编码 3）写：歌词几乎一定含中文，而 UTF-8 在 v2.3 里虽然不是
    // 标准允许的编码，实际播放器普遍都认，比 UTF-16 少一半体积也少一堆字节序坑。
    std::string MakeUsltBody(const std::string& lyric)
    {
        std::string body;
        body.push_back('\x03');             // UTF-8
        body += "und";                      // 语言未指定
        body.push_back('\0');               // 空描述
        body += lyric;
        return body;
    }

    bool IsPictureOrLyricFrame(const std::string& id)
    {
        return id == "APIC" || id == "USLT";
    }

    // ---- FLAC ----

    struct FlacBlock
    {
        int type{};
        std::string body;
    };

    // 解析元数据区。元数据不完整（head 读少了）时返回 false。
    bool ParseFlacBlocks(const std::string& head, std::vector<FlacBlock>& blocks,
                         size_t& audio_offset)
    {
        if (head.size() < 4 || head.compare(0, 4, "fLaC") != 0)
            return false;

        size_t offset = 4;
        while (true)
        {
            if (offset + 4 > head.size())
                return false;               // 块头都读不全，说明缓冲区太小
            const unsigned char flags = Byte(head, offset);
            const bool last = (flags & 0x80) != 0;
            const int type = flags & 0x7F;
            const uint32_t length = ReadBE24(head, offset + 1);
            if (offset + 4 + length > head.size())
                return false;

            FlacBlock block;
            block.type = type;
            block.body = head.substr(offset + 4, length);
            blocks.push_back(block);

            offset += 4 + length;
            if (last)
                break;
        }
        audio_offset = offset;
        return true;
    }

    std::string SerializeFlacBlocks(const std::vector<FlacBlock>& blocks)
    {
        std::string out = "fLaC";
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const bool last = (i + 1 == blocks.size());
            out.push_back(static_cast<char>((last ? 0x80 : 0x00) | (blocks[i].type & 0x7F)));
            AppendBE24(out, static_cast<uint32_t>(blocks[i].body.size()));
            out += blocks[i].body;
        }
        return out;
    }

    // FLAC PICTURE 块。宽高色深填 0 是规范允许的"未提供"，
    // 填这几个值就得真去解析 JPEG/PNG 的尺寸，不值当。
    std::string MakeFlacPicture(const AudioTag::Picture& picture)
    {
        const std::string mime = picture.mime.empty() ? std::string("image/jpeg") : picture.mime;
        std::string body;
        AppendBE32(body, static_cast<uint32_t>(picture.type == 0 ? 3 : picture.type));
        AppendBE32(body, static_cast<uint32_t>(mime.size()));
        body += mime;
        AppendBE32(body, 0);                // 描述长度
        AppendBE32(body, 0);                // 宽
        AppendBE32(body, 0);                // 高
        AppendBE32(body, 0);                // 色深
        AppendBE32(body, 0);                // 索引色数量
        AppendBE32(body, static_cast<uint32_t>(picture.data.size()));
        body += picture.data;
        return body;
    }

    // 在 VORBIS_COMMENT 里换掉某个字段，其余字段原样保留。
    // old_body 为空时按"空注释块"处理。
    std::string ReplaceVorbisField(const std::string& old_body, const std::string& key,
                                   const std::string& value)
    {
        std::string vendor = "MusicPlayer2 for Switch";
        std::vector<std::string> entries;

        if (old_body.size() >= 8)
        {
            const uint32_t vendor_len = ReadLE32(old_body, 0);
            if (4 + vendor_len + 4 <= old_body.size())
            {
                vendor = old_body.substr(4, vendor_len);
                size_t offset = 4 + vendor_len;
                const uint32_t count = ReadLE32(old_body, offset);
                offset += 4;
                for (uint32_t i = 0; i < count && offset + 4 <= old_body.size(); ++i)
                {
                    const uint32_t length = ReadLE32(old_body, offset);
                    offset += 4;
                    if (offset + length > old_body.size())
                        break;
                    entries.push_back(old_body.substr(offset, length));
                    offset += length;
                }
            }
        }

        // 字段名不区分大小写，删旧的时候要按大写比
        const std::string prefix = StringUtil::ToLower(key) + "=";
        std::vector<std::string> kept;
        for (const std::string& entry : entries)
        {
            if (StringUtil::ToLower(entry.substr(0, prefix.size())) == prefix)
                continue;
            kept.push_back(entry);
        }
        if (!value.empty())
            kept.push_back(key + "=" + value);

        std::string body;
        AppendLE32(body, static_cast<uint32_t>(vendor.size()));
        body += vendor;
        AppendLE32(body, static_cast<uint32_t>(kept.size()));
        for (const std::string& entry : kept)
        {
            AppendLE32(body, static_cast<uint32_t>(entry.size()));
            body += entry;
        }
        return body;
    }

    // FLAC 的元数据区可能很大（内嵌封面），一次读不够就翻倍再读。
    // 上限是防止一个损坏的长度字段把我们拖去读整个文件。
    const size_t kFlacHeadStart = 256 * 1024;
    const size_t kFlacHeadLimit = 8 * 1024 * 1024;
}

const char* TagWriter::ResultText(Result result)
{
    switch (result)
    {
    case RESULT_OK:             return T("已写入文件");
    case RESULT_UNSUPPORTED:    return T("这种格式不支持嵌入");
    case RESULT_READ_FAILED:    return T("读不了原文件");
    case RESULT_PARSE_FAILED:   return T("标签结构不认识，未改动原文件");
    case RESULT_WRITE_FAILED:   return T("写临时文件失败，未改动原文件");
    case RESULT_REPLACE_FAILED: return T("替换原文件失败，原文件仍然完好");
    }
    return T("未知错误");
}

bool TagWriter::CanEmbed(const std::string& file_path)
{
    const std::string ext = StringUtil::ToLower(FileUtil::GetExtension(file_path));
    return ext == "mp3" || ext == "flac";
}

bool TagWriter::BuildId3v2(const std::string& old_head, const AudioTag::Picture* cover,
                           const std::string* lyric, std::string& out_tag, size_t& audio_offset)
{
    out_tag.clear();
    audio_offset = 0;

    int version = 3;                        // 没有旧标签时新建 v2.3
    std::string kept_frames;

    const bool has_tag = (old_head.size() >= 10 && old_head.compare(0, 3, "ID3") == 0);
    if (has_tag)
    {
        version = Byte(old_head, 3);
        const uint32_t body_size = ReadSyncsafe(old_head, 6);
        audio_offset = 10 + body_size;
        if ((Byte(old_head, 5) & 0x10) != 0)
            audio_offset += 10;             // 带页脚

        // v2.2 的帧头是 3 字节 ID + 3 字节长度，和 v2.3/v2.4 完全不同。
        // 它已经很罕见了，与其写一套转换逻辑不如直接放弃保留旧帧：
        // 换来的是不会把一个自己没测过的格式写坏。
        if (version == 3 || version == 4)
        {
            const size_t tag_end = std::min(old_head.size(), static_cast<size_t>(10 + body_size));
            size_t offset = 10;
            while (offset + 10 <= tag_end)
            {
                const std::string id = old_head.substr(offset, 4);
                if (id[0] == '\0')
                    break;                  // 进入填充区
                const uint32_t size = ReadFrameSize(old_head, offset + 4, version);
                if (size == 0 || offset + 10 + size > tag_end)
                    break;
                // 我们要写的这两种帧丢掉，其余原样保留
                if (!IsPictureOrLyricFrame(id))
                    kept_frames += old_head.substr(offset, 10 + size);
                offset += 10 + size;
            }
        }
        else
        {
            version = 3;                    // 输出仍然写成 v2.3
        }
    }

    std::string frames = kept_frames;
    if (cover != nullptr && !cover->data.empty())
        frames += MakeId3Frame("APIC", MakeApicBody(*cover), version);
    if (lyric != nullptr && !lyric->empty())
        frames += MakeId3Frame("USLT", MakeUsltBody(*lyric), version);

    out_tag = "ID3";
    out_tag.push_back(static_cast<char>(version));
    out_tag.push_back('\0');                // 修订号
    out_tag.push_back('\0');                // 标志位：不用同步安全化、不带扩展头
    AppendSyncsafe(out_tag, static_cast<uint32_t>(frames.size()));
    out_tag += frames;
    return true;
}

bool TagWriter::BuildFlacMetadata(const std::string& old_head, const AudioTag::Picture* cover,
                                  const std::string* lyric, std::string& out_metadata,
                                  size_t& audio_offset)
{
    out_metadata.clear();
    audio_offset = 0;

    std::vector<FlacBlock> blocks;
    if (!ParseFlacBlocks(old_head, blocks, audio_offset))
        return false;
    if (blocks.empty() || blocks.front().type != 0)
        return false;                       // 第一个块必须是 STREAMINFO

    std::vector<FlacBlock> rebuilt;
    std::string comment_body;
    bool has_comment = false;

    for (const FlacBlock& block : blocks)
    {
        if (block.type == 1)
            continue;                       // PADDING：反正要重写，不必留着
        if (block.type == 6 && cover != nullptr && !cover->data.empty())
            continue;                       // 旧封面，换成新的
        if (block.type == 4)
        {
            comment_body = block.body;
            has_comment = true;
            continue;                       // 注释块单独处理，最后再插回去
        }
        rebuilt.push_back(block);
    }

    if (lyric != nullptr && !lyric->empty())
    {
        comment_body = ReplaceVorbisField(comment_body, "LYRICS", *lyric);
        has_comment = true;
    }
    if (has_comment)
    {
        FlacBlock block;
        block.type = 4;
        block.body = comment_body;
        rebuilt.push_back(block);
    }

    if (cover != nullptr && !cover->data.empty())
    {
        FlacBlock block;
        block.type = 6;
        block.body = MakeFlacPicture(*cover);
        rebuilt.push_back(block);
    }

    out_metadata = SerializeFlacBlocks(rebuilt);
    return true;
}

namespace
{
    // 把新的标签区和原文件从 audio_offset 起的部分拼成一个临时文件。
    // 音频数据按块搬，不整个读进内存：一首无损动辄三四十兆，
    // Switch 上没必要为了改个封面去申请那么大一块。
    bool WriteRebuiltFile(const std::string& temp_path, const std::string& header,
                          const std::string& source_path, size_t audio_offset)
    {
        FILE* src = FileUtil::OpenFile(source_path, "rb");
        if (src == nullptr)
            return false;
        if (std::fseek(src, static_cast<long>(audio_offset), SEEK_SET) != 0)
        {
            std::fclose(src);
            return false;
        }

        FILE* dst = FileUtil::OpenFile(temp_path, "wb");
        if (dst == nullptr)
        {
            std::fclose(src);
            return false;
        }

        bool ok = (std::fwrite(header.data(), 1, header.size(), dst) == header.size());
        if (ok)
        {
            std::vector<char> buffer(64 * 1024);
            while (true)
            {
                const size_t got = std::fread(buffer.data(), 1, buffer.size(), src);
                if (got == 0)
                    break;
                if (std::fwrite(buffer.data(), 1, got, dst) != got)
                {
                    ok = false;
                    break;
                }
            }
            if (std::ferror(src) != 0)
                ok = false;
        }

        std::fclose(src);
        if (std::fclose(dst) != 0)
            ok = false;
        FileUtil::CommitDevice(temp_path);
        return ok;
    }
}

TagWriter::Result TagWriter::Embed(const std::string& file_path, const AudioTag::Picture* cover,
                                   const std::string* lyric)
{
    const bool want_cover = (cover != nullptr && !cover->data.empty());
    const bool want_lyric = (lyric != nullptr && !lyric->empty());
    if (!want_cover && !want_lyric)
        return RESULT_OK;                   // 没有要写的东西
    if (!CanEmbed(file_path))
        return RESULT_UNSUPPORTED;

    const std::string ext = StringUtil::ToLower(FileUtil::GetExtension(file_path));
    std::string header;
    size_t audio_offset = 0;

    if (ext == "mp3")
    {
        // ID3v2 的长度写在最开头的 10 字节里，读这么多就够算出音频起点；
        // 要保留的旧帧也在这段里，所以按标签实际长度再补读一次。
        std::string head;
        if (!FileUtil::ReadHead(file_path, 10, head) || head.size() < 10)
            return RESULT_READ_FAILED;
        size_t need = 10;
        if (head.compare(0, 3, "ID3") == 0)
            need = 10 + ReadSyncsafe(head, 6) + 10;
        if (!FileUtil::ReadHead(file_path, need, head))
            return RESULT_READ_FAILED;
        if (!BuildId3v2(head, cover, lyric, header, audio_offset))
            return RESULT_PARSE_FAILED;
    }
    else
    {
        // FLAC 的元数据区没有一个总长度字段，只能读一段试着解析，
        // 不够就翻倍重来。内嵌大封面的文件可能有好几兆。
        std::string head;
        bool parsed = false;
        for (size_t want = kFlacHeadStart; want <= kFlacHeadLimit; want *= 2)
        {
            if (!FileUtil::ReadHead(file_path, want, head))
                return RESULT_READ_FAILED;
            if (BuildFlacMetadata(head, cover, lyric, header, audio_offset))
            {
                parsed = true;
                break;
            }
            if (head.size() < want)
                break;                      // 已经读到文件尾了，再翻倍也没用
        }
        if (!parsed)
            return RESULT_PARSE_FAILED;
    }

    const std::string temp_path = file_path + ".mp2tmp";
    if (!WriteRebuiltFile(temp_path, header, file_path, audio_offset))
    {
        std::remove(temp_path.c_str());
        return RESULT_WRITE_FAILED;
    }

    // 新文件至少要比标签区长，否则说明音频部分没搬过去。
    // 这一步很便宜，而失手的代价是把用户的歌换成一个空壳。
    if (FileUtil::GetFileSize(temp_path) <= header.size())
    {
        std::remove(temp_path.c_str());
        return RESULT_WRITE_FAILED;
    }

    if (!FileUtil::MoveOverwrite(temp_path, file_path))
    {
        std::remove(temp_path.c_str());
        return RESULT_REPLACE_FAILED;
    }
    return RESULT_OK;
}
