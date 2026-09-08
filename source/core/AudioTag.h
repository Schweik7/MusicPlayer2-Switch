#pragma once
#include <string>

// 音频文件标签读取。
//
// 为什么需要它：Switch 的文件系统处理不了非 ASCII 文件名，把中文歌传上去
// 只能先转成拼音（GBAStation 这类传输工具也是这么做的）。于是列表里全是
// "zhou_jie_lun_-_qing_tian.mp3"。但文件内部的标签是好的 —— 标签里存的是
// UTF-8/UTF-16 文本，跟文件系统无关，读出来就能显示真正的中文标题。
//
// 不引入 taglib：它体积大、构建慢，而我们只需要从几种容器里取出四五个字段。
// 这里只解析标签所在的那一小块，其余字节一律跳过。
namespace AudioTag
{
    struct Tag
    {
        std::string title;
        std::string artist;
        std::string album;
        std::string genre;
        int track{};
        int year{};

        bool IsEmpty() const
        {
            return title.empty() && artist.empty() && album.empty();
        }
    };

    // 从文件读取。识别不了或没有标签时返回 false，out 保持为空。
    bool Read(const std::string& file_path, Tag& out);

    // ---- 分格式解析器 ----
    // 单独暴露出来是为了能用内存里的样本做单元测试：
    // 标签解析全是偏移量和长度计算，正是最该测的那类代码。

    // MP3 头部的 ID3v2（支持 v2.2 / v2.3 / v2.4）
    bool ParseId3v2(const std::string& data, Tag& out);
    // 文件末尾 128 字节的 ID3v1
    bool ParseId3v1(const std::string& tail, Tag& out);
    // FLAC 的 VORBIS_COMMENT 元数据块
    bool ParseFlac(const std::string& data, Tag& out);
    // Ogg Vorbis / Opus 里的 Vorbis comment
    bool ParseOgg(const std::string& data, Tag& out);

    // 解析一段裸的 Vorbis comment 结构（FLAC 与 Ogg 共用同一种格式）
    bool ParseVorbisComment(const std::string& data, size_t offset, Tag& out);
}
