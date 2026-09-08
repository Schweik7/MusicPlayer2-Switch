#pragma once
#include "AudioTag.h"

#include <cstdint>
#include <string>

// 把下载来的封面和歌词写回音频文件本身。
//
// 为什么不用 ffmpeg / taglib：根本不需要。封面和歌词都存在文件开头的标签区里，
// 音频帧一个字节都不用动——重写标签区，再把原来的音频数据原样接在后面就完了。
// 解码是重活，改标签不是。
//
// 支持 MP3（ID3v2 的 APIC / USLT 帧）和 FLAC（PICTURE 块 / VORBIS_COMMENT）。
// Ogg 和 Opus 不支持：它们的注释头包在 Ogg 页里，改一个字段要重新分页并重算
// 每一页的 CRC，那才是真的需要一个容器库。
//
// 写入策略：先完整写出一个临时文件，确认无误再顶替原文件。绝不原地改写——
// 中途断电或写失败会毁掉用户的音乐，而这些文件多半没有备份。
namespace TagWriter
{
    enum Result
    {
        RESULT_OK = 0,
        RESULT_UNSUPPORTED,     // 这个容器不支持写入
        RESULT_READ_FAILED,     // 原文件读不了
        RESULT_PARSE_FAILED,    // 原文件的标签区看不懂，不敢动
        RESULT_WRITE_FAILED,    // 临时文件写不出来
        RESULT_REPLACE_FAILED   // 写出来了但换不回去，原文件仍然完好
    };

    const char* ResultText(Result result);

    // 这个文件能不能嵌入
    bool CanEmbed(const std::string& file_path);

    // 把封面和/或歌词写进 file_path。两者都可以为 nullptr，表示这一项不改。
    // 成功后原文件被替换，失败时原文件保持不动。
    Result Embed(const std::string& file_path, const AudioTag::Picture* cover,
                 const std::string* lyric);

    // ---- 下面是纯内存的组装函数，单独暴露以便测试 ----
    // 标签写入全是长度字段和偏移量，写错了会毁文件，必须能用内存样本反复验。

    // 生成一个新的 ID3v2 标签区（含 10 字节头）。
    // old_head 是原文件的开头若干字节：里面有 ID3v2 就沿用它的版本并保留其它帧，
    // 没有就新建一个 v2.3 标签。audio_offset 返回原文件里音频数据的起始偏移。
    bool BuildId3v2(const std::string& old_head, const AudioTag::Picture* cover,
                    const std::string* lyric, std::string& out_tag, size_t& audio_offset);

    // 生成一个新的 FLAC 元数据区（含 "fLaC" 标识）。
    // old_head 必须包含完整的元数据区，否则返回 false（调用方应该多读一些再试）。
    bool BuildFlacMetadata(const std::string& old_head, const AudioTag::Picture* cover,
                           const std::string* lyric, std::string& out_metadata,
                           size_t& audio_offset);
}
