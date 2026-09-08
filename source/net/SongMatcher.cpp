#include "SongMatcher.h"
#include "../core/StringUtil.h"

#include <algorithm>
#include <cstdlib>

std::string DownloadItem::GetDisplayName() const
{
    if (artist.empty())
        return title;
    return artist + " - " + title;
}

namespace SongMatcher
{

namespace
{
    // 字符串长度上限，与桌面版一致
    const size_t kMaxLength = 256;

    // 阿拉伯数字与中文数字的对应关系
    struct DigitPair { char32_t digit; char32_t chinese; };
    const DigitPair kDigitPairs[] = {
        { U'0', U'零' }, { U'1', U'一' }, { U'2', U'二' }, { U'3', U'三' }, { U'4', U'四' },
        { U'5', U'五' }, { U'6', U'六' }, { U'7', U'七' }, { U'8', U'八' }, { U'9', U'九' },
    };
}

double CharacterSimilarDegree(char32_t ch1, char32_t ch2)
{
    if (ch1 == ch2)
        return 1.0;

    // 仅大小写不同
    if ((ch1 >= U'A' && ch1 <= U'Z' && ch2 == ch1 + 32)
        || (ch1 >= U'a' && ch1 <= U'z' && ch2 == ch1 - 32))
    {
        return 0.8;
    }

    for (const DigitPair& pair : kDigitPairs)
    {
        if ((ch1 == pair.digit && ch2 == pair.chinese) || (ch1 == pair.chinese && ch2 == pair.digit))
            return 0.7;
    }
    return 0.0;
}

double StringSimilarDegree(const std::string& src_utf8, const std::string& match_utf8)
{
    // 按码点而不是字节比较：一个汉字是 3 个字节，按字节算编辑距离会严重失真
    std::vector<char32_t> src = StringUtil::Utf8ToCodePoints(src_utf8);
    std::vector<char32_t> match = StringUtil::Utf8ToCodePoints(match_utf8);

    const size_t n = src.size();
    const size_t m = match.size();
    if (n == 0 || m == 0 || n > kMaxLength || m > kMaxLength)
        return 0.0;

    // 滚动数组的编辑距离，只保留两行，避免 256x256 的二维表
    std::vector<double> prev(m + 1);
    std::vector<double> curr(m + 1);
    for (size_t j = 0; j <= m; ++j)
        prev[j] = static_cast<double>(j);

    for (size_t i = 1; i <= n; ++i)
    {
        curr[0] = static_cast<double>(i);
        for (size_t j = 1; j <= m; ++j)
        {
            double cost = 1.0 - CharacterSimilarDegree(match[j - 1], src[i - 1]);
            curr[j] = std::min(std::min(prev[j] + 1.0, curr[j - 1] + 1.0), prev[j - 1] + cost);
        }
        prev.swap(curr);
    }

    double distance = prev[m];
    return 1.0 - distance / static_cast<double>(std::max(n, m));
}

double DurationSimilarDegree(int duration_a_ms, int duration_b_ms)
{
    if (duration_a_ms <= 0 || duration_b_ms <= 0)
        return 0.0;                             // 有一边不知道时长，这项不参与打分

    const int diff = std::abs(duration_a_ms - duration_b_ms);
    const int kExactMs = 2000;                  // 2 秒以内算完全一致
    const int kMaxMs = 15000;                   // 差 15 秒以上算完全不符
    if (diff <= kExactMs)
        return 1.0;
    if (diff >= kMaxMs)
        return 0.0;
    return 1.0 - static_cast<double>(diff - kExactMs) / (kMaxMs - kExactMs);
}

int SelectMatchedItem(const std::vector<DownloadItem>& list,
                      const std::string& title,
                      const std::string& artist,
                      const std::string& album,
                      const std::string& file_name,
                      int local_duration_ms)
{
    /*
    权值分配沿用桌面版：
        标题——标题       0.4
        艺术家——艺术家   0.4
        唱片集——唱片集   0.3
        文件名——标题     0.3
        文件名——艺术家   0.3
        列表中的排序      0.05
    另外加入桌面版没有的一项：
        时长接近程度      0.5
    时长权重给得比单项文字都高，是因为它几乎不会误导——同名不同版本
    （Live、伴奏、加长版）光看文字分不出来，时长却一目了然。
    本地时长未知时这项恒为 0，退化成原来的行为。
    */
    if (list.empty())
        return -1;

    const bool use_duration = (local_duration_ms > 0);
    const double kDurationWeight = 0.5;
    const double kTextThreshold = 0.3;

    // 时长分不能直接加进总分再统一比阈值：那样"文字毫不相干、时长碰巧一致"
    // 的项光靠 0.5 的时长分就能过线。正确的做法是先用文字分筛掉不像的，
    // 再让时长在剩下的候选里分高下。
    double best_total = -1.0;
    int best_index = -1;

    for (size_t i = 0; i < list.size(); ++i)
    {
        const DownloadItem& item = list[i];
        double text_weight = 0.0;
        text_weight += StringSimilarDegree(title, item.title) * 0.4;
        text_weight += StringSimilarDegree(artist, item.artist) * 0.4;
        text_weight += StringSimilarDegree(album, item.album) * 0.3;
        text_weight += StringSimilarDegree(file_name, item.title) * 0.3;
        text_weight += StringSimilarDegree(file_name, item.artist) * 0.3;

        // 搜索结果越靠前关联度一般越高：首项取 1，之后每项减 0.02
        text_weight += (1.0 - i * 0.02) * 0.05;

        if (text_weight < kTextThreshold)
            continue;                       // 文字都不像，时长再准也不要

        double total = text_weight;
        if (use_duration)
            total += DurationSimilarDegree(local_duration_ms, item.duration) * kDurationWeight;

        if (total > best_total)
        {
            best_total = total;
            best_index = static_cast<int>(i);
        }
    }

    return best_index;
}

}   // namespace SongMatcher
