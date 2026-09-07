#pragma once
#include "SongMatcher.h"

#include <string>
#include <vector>

// 歌词/封面下载源。
//
// 对应桌面版的 CLyricDownloadCommon 抽象。这里刻意只保留"构造 URL"和"解析响应"两件事，
// 完全不碰网络 IO，因此可以在开发机上用固定的响应样本做单元测试。
class ILyricProvider
{
public:
    virtual ~ILyricProvider() = default;

    virtual const char* GetName() const = 0;

    // ---- 搜索 ----
    virtual std::string GetSearchUrl(const std::string& keywords, int result_count) const = 0;
    // 网易云的搜索接口要求用 POST
    virtual bool SearchUsesPost() const { return false; }
    virtual void ParseSearchResult(const std::string& response, std::vector<DownloadItem>& out) const = 0;

    // ---- 歌词 ----
    virtual std::string GetLyricUrl(const std::string& song_id, bool with_translation) const = 0;
    // 解析成可直接写盘的 LRC 文本；失败返回 false
    virtual bool ParseLyric(const std::string& response, bool with_translation,
                            std::string& lyric_out) const = 0;

    // ---- 封面 ----
    // 先请求这个 URL 拿到详情，再用 ParseCoverUrl 从响应里取出真正的图片地址。
    // 返回空串表示该源不支持封面下载。
    virtual std::string GetCoverInfoUrl(const std::string& song_id) const = 0;
    virtual std::string ParseCoverUrl(const std::string& response) const = 0;

    // 某些源（QQ 音乐）必须带 Referer 才会返回数据
    virtual std::vector<std::string> GetExtraHeaders() const { return {}; }
};

// 网易云音乐
class CNeteaseProvider : public ILyricProvider
{
public:
    const char* GetName() const override { return "网易云音乐"; }

    std::string GetSearchUrl(const std::string& keywords, int result_count) const override;
    bool SearchUsesPost() const override { return true; }
    void ParseSearchResult(const std::string& response, std::vector<DownloadItem>& out) const override;

    std::string GetLyricUrl(const std::string& song_id, bool with_translation) const override;
    bool ParseLyric(const std::string& response, bool with_translation,
                    std::string& lyric_out) const override;

    std::string GetCoverInfoUrl(const std::string& song_id) const override;
    std::string ParseCoverUrl(const std::string& response) const override;
};

// QQ 音乐
class CQQMusicProvider : public ILyricProvider
{
public:
    const char* GetName() const override { return "QQ音乐"; }

    std::string GetSearchUrl(const std::string& keywords, int result_count) const override;
    void ParseSearchResult(const std::string& response, std::vector<DownloadItem>& out) const override;

    std::string GetLyricUrl(const std::string& song_id, bool with_translation) const override;
    bool ParseLyric(const std::string& response, bool with_translation,
                    std::string& lyric_out) const override;

    std::string GetCoverInfoUrl(const std::string& song_id) const override;
    std::string ParseCoverUrl(const std::string& response) const override;

    std::vector<std::string> GetExtraHeaders() const override;
};

namespace LyricProviderUtil
{
    // 在歌词开头补上 [id:]/[ti:]/[ar:]/[al:] 标签，移植自桌面版 AddLyricTag。
    // 已有的同名标签不会被覆盖（空标签如 "[ti:]" 视为不存在）。
    void AddLyricTag(std::string& lyric, const std::string& song_id, const std::string& title,
                     const std::string& artist, const std::string& album);

    // 有些接口即便指定了 format=json 仍会返回 JSONP，这里剥掉外层的 callback(...)
    std::string StripJsonp(const std::string& response);

    // 用歌曲信息拼出搜索关键词：优先"艺术家 标题"，都没有就用文件名
    std::string MakeSearchKeyword(const std::string& title, const std::string& artist,
                                  const std::string& file_name);
}
