#include "LrcParser.h"
#include "FileUtil.h"
#include "StringUtil.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>

// 时间标签里允许出现的字符集，用于把行首的（可能连续的）时间标签和歌词正文分开
static const char* kTimeTagChars = "[]<>:.0123456789-";

bool CLrcParser::ParseLyricTimeTag(const std::string& lyric_text, CPlayTime& time,
                                   int& pos_start, int& pos_end, char bracket_left, char bracket_right)
{
    // 时间标签全部由 ASCII 组成，UTF-8 的多字节序列不会与之冲突，故这里按字节下标搜索是安全的
    int index = pos_end - 1;
    const int size = static_cast<int>(lyric_text.size());
    bool time_acquired{ false };

    while (!time_acquired)
    {
        size_t found = lyric_text.find_first_of(bracket_left, index + 1);        // 查找左括号
        if (found == std::string::npos)
            break;
        index = static_cast<int>(found);
        if (index > size - 9)                                                    // 剩余长度不足以容纳一个时间标签
            break;
        if ((lyric_text[index + 1] > '9' || lyric_text[index + 1] < '0') && lyric_text[index + 1] != '-')
            continue;                                                            // 左括号后面不是数字也不是负号

        size_t index1 = lyric_text.find_first_of(':', index);                    // 分钟和秒之间的冒号
        if (index1 == std::string::npos)
            continue;
        size_t index2 = lyric_text.find_first_of(".:", index1 + 1);              // 秒和毫秒之间的圆点（兼容用冒号分隔）
        if (index2 == std::string::npos)
            continue;
        size_t index3 = lyric_text.find_first_of(bracket_right, index2 + 1);     // 右括号
        if (index3 == std::string::npos)
            continue;

        std::string temp = lyric_text.substr(index + 1, index1 - index - 1);
        time.min = static_cast<unsigned int>(StringUtil::ToInt(temp));
        temp = lyric_text.substr(index1 + 1, index2 - index1 - 1);
        time.sec = static_cast<unsigned int>(StringUtil::ToInt(temp));
        temp = lyric_text.substr(index2 + 1, index3 - index2 - 1);

        int char_cnt = static_cast<int>(temp.size());                            // 毫秒部分的位数
        if (char_cnt > 0 && temp[0] == '-')
            --char_cnt;
        switch (char_cnt)
        {
        case 0:
            time.msec = 0;
            break;
        case 1:
            time.msec = static_cast<unsigned int>(StringUtil::ToInt(temp) * 100);
            break;
        case 2:
            time.msec = static_cast<unsigned int>(StringUtil::ToInt(temp) * 10);
            break;
        default:
            time.msec = static_cast<unsigned int>(StringUtil::ToInt(temp) % 1000);
            break;
        }

        time_acquired = true;
        pos_start = index;
        pos_end = static_cast<int>(index3) + 1;
    }
    return time_acquired;
}

void CLrcParser::Clear()
{
    m_lyrics.clear();
    m_ti.clear(); m_ar.clear(); m_al.clear(); m_by.clear(); m_id.clear();
    m_ti_tag = m_ar_tag = m_al_tag = m_by_tag = m_id_tag = m_offset_tag = false;
    m_offset = 0;
    m_translate = false;
}

bool CLrcParser::ParseString(const std::string& utf8_content)
{
    Clear();
    std::vector<std::string> lines;
    StringUtil::SplitLine(utf8_content, lines);
    DisposeLrc(lines);
    NormalizeLyric();
    return !m_lyrics.empty();
}

bool CLrcParser::ParseFile(const std::string& file_path)
{
    std::string content;
    if (!FileUtil::ReadAll(file_path, content))
    {
        Clear();
        return false;
    }
    return ParseString(StringUtil::FileContentToUtf8(std::move(content)));
}

void CLrcParser::DisposeLrc(const std::vector<std::string>& lines)
{
    m_translate = false;
    for (const std::string& str : lines)
    {
        size_t index = str.find('[');
        if (index == std::string::npos)
            continue;
        size_t index2 = str.find(']', index);
        if (index2 == std::string::npos)                    // 略过没有右方括号的行
            continue;

        // ---- 元数据标签 ----
        struct { const char* tag; bool* flag; std::string* value; } meta[] = {
            { "[id:", &m_id_tag, &m_id },
            { "[ti:", &m_ti_tag, &m_ti },
            { "[ar:", &m_ar_tag, &m_ar },
            { "[al:", &m_al_tag, &m_al },
            { "[by:", &m_by_tag, &m_by },
        };
        for (auto& m : meta)
        {
            if (*m.flag)
                continue;
            size_t pos = str.find(m.tag);
            if (pos == std::string::npos)
                continue;
            size_t close = str.find(']', pos);
            if (close == std::string::npos)
                continue;
            *m.flag = true;
            size_t tag_len = std::char_traits<char>::length(m.tag);
            *m.value = StringUtil::Trimmed(str.substr(pos + tag_len, close - pos - tag_len));
        }
        if (!m_offset_tag)
        {
            size_t pos = str.find("[offset:");
            size_t close = (pos == std::string::npos) ? std::string::npos : str.find(']', pos);
            if (close != std::string::npos)
            {
                m_offset_tag = true;
                m_offset = StringUtil::ToInt(str.substr(pos + 8, close - pos - 8));
            }
        }

        // ---- 时间轴 ----
        CPlayTime t{};
        int pos_start{}, pos_end{};
        char bracket_left{ '[' }, bracket_right{ ']' };
        if (ParseLyricTimeTag(str, t, pos_start, pos_end, '<', '>'))
        {
            // 存在尖括号时间标签则按 ESLyric 0.5.x 解析，丢弃首个 [] 时间标签
            bracket_left = '<';
            bracket_right = '>';
        }
        t.fromInt(0);                                       // 重置搜索状态
        pos_start = pos_end = 0;
        if (!ParseLyricTimeTag(str, t, pos_start, pos_end, bracket_left, bracket_right))
            continue;                                       // 没有时间标签的行不是歌词

        Lyric lyric;
        std::string time_str, text_str;
        size_t sep = str.find_first_not_of(kTimeTagChars, pos_end);
        if (sep != std::string::npos)
        {
            sep = str.rfind(bracket_right, sep);            // 避免把歌词开头的数字截进时间串
            sep = (sep == std::string::npos) ? static_cast<size_t>(pos_end) : sep + 1;
            time_str = str.substr(0, sep);
            text_str = str.substr(sep);
        }
        else
        {
            time_str = str;
        }
        StringUtil::Trim(text_str);

        if (!text_str.empty())
        {
            size_t slash = text_str.find(" / ");            // 提取翻译
            if (slash != std::string::npos)
            {
                lyric.translate = text_str.substr(slash + 3);
                text_str = text_str.substr(0, slash);
                m_translate = true;
            }

            int w_start{}, w_end{};
            CPlayTime time_w, time_w_;
            if (ParseLyricTimeTag(text_str, time_w_, w_start, w_end, bracket_left, bracket_right))
            {
                // 正文里还有时间标签，说明是逐字（增强）LRC
                lyric.text = text_str.substr(0, w_start);
                lyric.split.push_back(lyric.text.size());
                lyric.word_time.push_back(time_w_ - t);
                int last_pos_end = w_end;
                while (ParseLyricTimeTag(text_str, time_w, w_start, w_end, bracket_left, bracket_right))
                {
                    lyric.text += text_str.substr(last_pos_end, w_start - last_pos_end);
                    lyric.split.push_back(lyric.text.size());
                    lyric.word_time.push_back(time_w - time_w_);
                    last_pos_end = w_end;
                    time_w_ = time_w;
                }
                if (last_pos_end < static_cast<int>(text_str.size()))
                {
                    // 最后一个时间标签之后还有文字：作为一个没有显式时长的匀速段
                    lyric.text += text_str.substr(last_pos_end);
                    lyric.split.push_back(lyric.text.size());
                    lyric.word_time.push_back(-1);
                }
            }
            else
            {
                lyric.text = text_str;
            }
        }

        // 压缩 LRC 在此展开（压缩时间标签只能是方括号）
        do
        {
            lyric.time_start_raw = t.toInt();
            m_lyrics.push_back(lyric);
        } while (ParseLyricTimeTag(time_str, t, pos_start, pos_end, '[', ']'));
    }

    CombineSameTimeLyric();
}

void CLrcParser::CombineSameTimeLyric(int error)
{
    if (m_lyrics.size() < 2)
        return;
    std::stable_sort(m_lyrics.begin(), m_lyrics.end());

    std::vector<Lyric> combined;
    combined.push_back(m_lyrics.front());
    for (size_t i = 1; i < m_lyrics.size(); ++i)
    {
        Lyric& prev = combined.back();
        const Lyric& cur = m_lyrics[i];
        if (std::abs(cur.time_start_raw - prev.time_start_raw) <= error
            && prev.translate.empty() && cur.split.empty() && prev.split.empty())
        {
            // 时间相同的两行：后一行视为前一行的翻译（网易云等来源的双语歌词）
            if (!cur.text.empty())
            {
                if (prev.text.empty())
                    prev.text = cur.text;
                else
                {
                    prev.translate = cur.text;
                    m_translate = true;
                }
            }
            continue;
        }
        combined.push_back(cur);
    }
    m_lyrics = std::move(combined);
}

void CLrcParser::NormalizeLyric()
{
    if (m_lyrics.empty())
        return;
    std::stable_sort(m_lyrics.begin(), m_lyrics.end());

    int total_offset = m_offset + m_user_offset;
    int last{};
    // 填充 time_start，应用偏移量同时避免出现重叠
    for (size_t i = 0; i < m_lyrics.size(); ++i)
    {
        last = std::max(last, m_lyrics[i].time_start_raw + total_offset);
        m_lyrics[i].time_start = last;
        last += 10;
    }

    for (size_t i = 0; i + 1 < m_lyrics.size(); ++i)
    {
        Lyric& now = m_lyrics[i];
        const Lyric& next = m_lyrics[i + 1];
        if (!now.word_time.empty() && now.word_time.back() < 0)
        {
            // 逐字歌词的最后一段没有显式时长，认为其持续到下一行开始
            now.word_time.back() = next.time_start - now.time_start
                                 - std::accumulate(now.word_time.begin(), now.word_time.end() - 1, 0);
            if (now.word_time.back() < 0)
                now.word_time.back() = 0;
        }
        if (now.time_span_raw != 0)
            now.time_span = now.time_span_raw;
        else if (!now.word_time.empty())
            now.time_span = std::accumulate(now.word_time.begin(), now.word_time.end(), 0);
        // 不是逐字歌词，或逐字时长超过了下一句的开始，都以下一句开始时间为准
        if (now.time_span == 0 || next.time_start - now.time_start < now.time_span)
            now.time_span = next.time_start - now.time_start;
    }

    Lyric& last_lyric = m_lyrics.back();
    if (!last_lyric.word_time.empty() && last_lyric.word_time.back() < 0)
    {
        if (last_lyric.word_time.size() >= 2)
            last_lyric.word_time.back() = *(last_lyric.word_time.end() - 2);
        else
            last_lyric.word_time.back() = 20000;            // 兜底 20 秒
        if (last_lyric.word_time.back() < 0)
            last_lyric.word_time.back() = 0;
    }
    if (last_lyric.time_span_raw != 0)
        last_lyric.time_span = last_lyric.time_span_raw;
    else if (!last_lyric.word_time.empty())
        last_lyric.time_span = std::accumulate(last_lyric.word_time.begin(), last_lyric.word_time.end(), 0);
}

void CLrcParser::SetUserOffset(int offset_ms)
{
    m_user_offset = offset_ms;
    NormalizeLyric();
}

bool CLrcParser::IsKaraoke() const
{
    for (const Lyric& lyric : m_lyrics)
    {
        if (!lyric.split.empty())
            return true;
    }
    return false;
}

int CLrcParser::GetLyricIndex(int time_ms) const
{
    if (m_lyrics.empty() || time_ms < m_lyrics[0].time_start)
        return -1;
    // 找到最后一个 time_start <= time_ms 的行
    size_t lo = 0, hi = m_lyrics.size();
    while (lo + 1 < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (m_lyrics[mid].time_start <= time_ms)
            lo = mid;
        else
            hi = mid;
    }
    return static_cast<int>(lo);
}

double CLrcParser::GetLyricProgress(int time_ms) const
{
    int index = GetLyricIndex(time_ms);
    if (index < 0)
        return 0.0;
    const Lyric& lyric = m_lyrics[index];
    if (lyric.time_span <= 0)
        return 0.0;
    double progress = static_cast<double>(time_ms - lyric.time_start) / lyric.time_span;
    if (progress < 0.0) return 0.0;
    if (progress > 1.0) return 1.0;
    return progress;
}

std::string CLrcParser::FindLyricFile(const std::string& audio_file_path)
{
    if (audio_file_path.empty())
        return std::string();
    // 依次尝试 .lrc / .LRC，以及同目录下的 lyrics 子目录
    const char* exts[] = { ".lrc", ".LRC" };
    for (const char* ext : exts)
    {
        std::string candidate = FileUtil::ReplaceExtension(audio_file_path, ext);
        if (FileUtil::Exists(candidate))
            return candidate;
    }
    std::string in_subdir = FileUtil::Combine(FileUtil::Combine(FileUtil::GetDir(audio_file_path), "lyrics"),
                                              FileUtil::GetFileNameWithoutExt(audio_file_path) + ".lrc");
    if (FileUtil::Exists(in_subdir))
        return in_subdir;
    return std::string();
}
