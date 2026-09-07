#pragma once
#include <string>
#include <vector>

// 一条搜索结果，对应桌面版 CLyricDownloadCommon::ItemInfo
struct DownloadItem
{
    std::string id;             // 歌曲 ID（网易云是数字，QQ 是 songmid）
    std::string title;
    std::string artist;
    std::string album;
    int duration{};             // 毫秒，未知为 0
    int track{};

    std::string GetDisplayName() const;
};

// 从搜索结果里挑出与本地文件最匹配的一项。
// 算法移植自桌面版 CInternetCommon::StringSimilarDegree_LD 与
// CLyricDownloadCommon::SelectMatchedItem，权值保持一致。
namespace SongMatcher
{
    // 单个字符的相似度：完全相同 1.0，仅大小写不同 0.8，阿拉伯数字与中文数字互换 0.7
    double CharacterSimilarDegree(char32_t ch1, char32_t ch2);

    // 基于编辑距离的字符串相似度，返回 0.0~1.0。
    // 任一侧为空、或长度超过 256 个字符时返回 0（与桌面版一致，避免大串上的 O(n*m) 开销）。
    double StringSimilarDegree(const std::string& src_utf8, const std::string& match_utf8);

    // 返回最匹配项的下标；没有达到阈值时返回 -1
    int SelectMatchedItem(const std::vector<DownloadItem>& list,
                          const std::string& title,
                          const std::string& artist,
                          const std::string& album,
                          const std::string& file_name);
}
