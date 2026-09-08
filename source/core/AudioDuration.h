#pragma once
#include <cstdint>
#include <string>

// 音频时长估算。
//
// 为什么需要它：devkitPro 带的 SDL2_mixer 是 2.0.4，没有 Mix_MusicDuration
// （那是 2.6.0 才加的接口）。于是除 FLAC（走我们自己的解码器，能从 STREAMINFO
// 直接读出总采样数）以外的格式，进度条右侧永远是 "-:--"，也没法拖动定位。
//
// 这里不解码，只读文件头尾的若干 KB 把时长算出来：
//   MP3  —— 跳过 ID3v2，读第一个帧头；帧内若带 Xing/Info/VBRI 就用其中的帧数，
//           否则按码率和音频数据长度做 CBR 估算
//   WAV  —— fmt 块的字节率 + data 块长度
//   OGG/Opus —— 末尾最后一个 Ogg 页的 granule position 就是总采样数
//
// 全部是偏移量与长度计算，因此放在平台无关的 core/ 里，可以用内存样本做单元测试。
namespace AudioDuration
{
    // 需要读入的头部与尾部字节数。调用方按这个大小取数据即可。
    const size_t kHeadBytes = 64 * 1024;
    const size_t kTailBytes = 64 * 1024;

    // 从文件读取并估算，单位毫秒；识别不了返回 0。
    int Estimate(const std::string& file_path);

    // 上面那个的纯内存版本，测试用。
    // ext 是不带点的小写扩展名，head/tail 分别是文件头尾的若干字节，
    // file_size 是文件总长度（head 覆盖整个文件时也要给准确值）。
    int EstimateFromBuffers(const std::string& ext, const std::string& head,
                            const std::string& tail, uint64_t file_size);

    // ---- 分格式解析器，单独暴露以便测试 ----

    // MP3。head 从文件开头算起，内部会跳过 ID3v2。
    // 第一个音频帧不在 head 里（内嵌封面动辄几百 KB）时返回 0，
    // 这种情况 Estimate 会改用下面那个按偏移读的版本。
    int Mp3DurationMs(const std::string& head, uint64_t file_size);
    // audio 是从文件偏移 audio_offset 处读到的一段，且该偏移已经在 ID3v2 之后。
    int Mp3DurationFromAudio(const std::string& audio, uint64_t audio_offset, uint64_t file_size);
    // RIFF/WAVE
    int WavDurationMs(const std::string& head, uint64_t file_size);
    // Ogg 容器里的 Vorbis 或 Opus
    int OggDurationMs(const std::string& head, const std::string& tail);
    // FLAC 的 STREAMINFO（总采样数 / 采样率）
    int FlacDurationMs(const std::string& head);
}
