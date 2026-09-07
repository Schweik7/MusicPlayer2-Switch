#pragma once
#include "PlayTime.h"
#include <string>
#include <vector>

// LRC 歌词解析，逻辑移植自桌面版 Lyric.cpp 的 ParseLyricTimeTag / DisposeLrc / NormalizeLyric。
// 与桌面版的差异：字符串为 UTF-8，split 存的是**字节**偏移（因为时间标签都是 ASCII，
// 分割点必定落在 UTF-8 字符边界上，直接用 substr 是安全的）。
//
// 支持：
//   - 标准 LRC：[mm:ss.xx]歌词
//   - 压缩 LRC：[00:01.00][00:30.00]同一句歌词
//   - 增强 LRC（逐字/卡拉OK）：[00:01.00]<00:01.00>逐<00:01.50>字
//   - ESLyric 0.5.x 尖括号时间轴
//   - 元数据标签 [ti:] [ar:] [al:] [by:] [offset:] [id:]
//   - " / " 分隔的翻译
class CLrcParser
{
public:
    struct Lyric
    {
        int time_start_raw{};           // 行开始时间（解析所得，未应用偏移）
        int time_span_raw{};            // 行持续时间（解析所得）
        int time_start{};               // 行开始时间（已应用偏移）
        int time_span{};                // 行持续时间（已归一化）
        std::string text;
        std::string translate;
        std::vector<size_t> split;      // 逐字分段的**字节**结束偏移，与 word_time 一一对应
        std::vector<int> word_time;     // 每个分段的持续时长（毫秒）

        bool operator<(const Lyric& other) const { return time_start_raw < other.time_start_raw; }
    };

    bool ParseString(const std::string& utf8_content);
    bool ParseFile(const std::string& file_path);
    void Clear();

    bool IsEmpty() const { return m_lyrics.empty(); }
    bool HasTranslation() const { return m_translate; }
    // 逐字歌词（卡拉OK）判定：任意一行带有分段信息
    bool IsKaraoke() const;

    const std::vector<Lyric>& GetLyrics() const { return m_lyrics; }
    const std::string& GetTitle() const { return m_ti; }
    const std::string& GetArtist() const { return m_ar; }
    const std::string& GetAlbum() const { return m_al; }
    const std::string& GetBy() const { return m_by; }
    int GetOffset() const { return m_offset; }

    // 当前时间对应的歌词行下标；早于第一句时返回 -1
    int GetLyricIndex(int time_ms) const;
    // 当前行的演唱进度 0.0~1.0，用于卡拉OK描色
    double GetLyricProgress(int time_ms) const;

    // 在偏移量之外再叠加一个用户手动调整量（毫秒，正数表示歌词延后）
    void SetUserOffset(int offset_ms);
    int  GetUserOffset() const { return m_user_offset; }

    // 与歌曲同名的 .lrc 文件路径；找不到返回空
    static std::string FindLyricFile(const std::string& audio_file_path);

private:
    // 从 pos_end 之后查找下一个时间标签。找到则写回 time / pos_start / pos_end 并返回 true。
    static bool ParseLyricTimeTag(const std::string& lyric_text, CPlayTime& time,
                                  int& pos_start, int& pos_end, char bracket_left, char bracket_right);
    void DisposeLrc(const std::vector<std::string>& lines);
    void CombineSameTimeLyric(int error = 0);
    void NormalizeLyric();

    std::vector<Lyric> m_lyrics;
    std::string m_ti, m_ar, m_al, m_by, m_id;
    bool m_ti_tag{}, m_ar_tag{}, m_al_tag{}, m_by_tag{}, m_id_tag{}, m_offset_tag{};
    int m_offset{};
    int m_user_offset{};
    bool m_translate{};
};
