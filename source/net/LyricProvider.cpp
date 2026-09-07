#include "LyricProvider.h"
#include "Json.h"
#include "UrlUtil.h"
#include "../core/StringUtil.h"

#include <cstdio>

// ============================================================ 网易云音乐

std::string CNeteaseProvider::GetSearchUrl(const std::string& keywords, int result_count) const
{
    char buff[512];
    std::snprintf(buff, sizeof(buff),
                  "http://music.163.com/api/search/get/?s=%s&limit=%d&type=1&offset=0",
                  UrlUtil::Encode(keywords).c_str(), result_count);
    return buff;
}

void CNeteaseProvider::ParseSearchResult(const std::string& response,
                                         std::vector<DownloadItem>& out) const
{
    out.clear();

    JsonValue root;
    if (!JsonValue::Parse(LyricProviderUtil::StripJsonp(response), root))
        return;

    const JsonValue& songs = root["result"]["songs"];
    if (!songs.IsArray())
        return;

    for (const JsonValue& song : songs.Elements())
    {
        DownloadItem item;
        // id 是数字，转成字符串后续统一按字符串处理
        item.id = std::to_string(song.GetInt("id", 0));
        if (item.id == "0")
            continue;
        item.title = song.GetString("name");
        item.duration = static_cast<int>(song.GetInt("duration", 0));
        item.album = song["album"].GetString("name");

        const JsonValue& artists = song["artists"];
        if (artists.IsArray())
        {
            for (const JsonValue& artist : artists.Elements())
            {
                std::string name = artist.GetString("name");
                if (name.empty())
                    continue;
                if (!item.artist.empty())
                    item.artist += '/';
                item.artist += name;
            }
        }
        out.push_back(std::move(item));
    }
}

std::string CNeteaseProvider::GetLyricUrl(const std::string& song_id, bool with_translation) const
{
    // 带翻译时用 song/lyric 接口（返回 lrc / tlyric 两段）；
    // 不带翻译时 song/media 更轻，但字段名不同，ParseLyric 里两种都认
    if (with_translation)
        return "http://music.163.com/api/song/lyric?os=osx&id=" + song_id + "&lv=-1&kv=-1&tv=-1";
    return "http://music.163.com/api/song/media?id=" + song_id;
}

bool CNeteaseProvider::ParseLyric(const std::string& response, bool with_translation,
                                  std::string& lyric_out) const
{
    lyric_out.clear();

    JsonValue root;
    if (!JsonValue::Parse(LyricProviderUtil::StripJsonp(response), root))
        return false;

    // song/lyric 返回 {"lrc":{"lyric":"..."},"tlyric":{"lyric":"..."}}
    // song/media 返回 {"lyric":"..."}
    std::string lyric = root["lrc"].GetString("lyric");
    if (lyric.empty())
        lyric = root.GetString("lyric");
    if (lyric.empty())
        return false;

    lyric_out = lyric;

    if (with_translation)
    {
        std::string translation = root["tlyric"].GetString("lyric");
        if (!translation.empty())
        {
            // 翻译是一份时间轴相同的独立 LRC。直接追加即可：
            // CLrcParser::CombineSameTimeLyric 会把时间相同的两行合并成"原文 + 翻译"
            if (lyric_out.back() != '\n')
                lyric_out += '\n';
            lyric_out += translation;
        }
    }
    return true;
}

std::string CNeteaseProvider::GetCoverInfoUrl(const std::string& song_id) const
{
    if (song_id.empty())
        return std::string();
    // ids 参数需要是 URL 编码后的 [id]
    return "http://music.163.com/api/song/detail/?id=" + song_id
         + "&ids=%5B" + song_id + "%5D&csrf_token=";
}

std::string CNeteaseProvider::ParseCoverUrl(const std::string& response) const
{
    JsonValue root;
    if (!JsonValue::Parse(LyricProviderUtil::StripJsonp(response), root))
        return std::string();

    const JsonValue& songs = root["songs"];
    if (!songs.IsArray() || songs.Size() == 0)
        return std::string();

    std::string url = songs[0]["album"].GetString("picUrl");
    if (url.empty())
        url = songs[0]["album"].GetString("blurPicUrl");
    return url;
}

// ============================================================ QQ 音乐

std::string CQQMusicProvider::GetSearchUrl(const std::string& keywords, int result_count) const
{
    char buff[512];
    std::snprintf(buff, sizeof(buff),
                  "https://c.y.qq.com/soso/fcgi-bin/client_search_cp?p=1&n=%d&w=%s&format=json",
                  result_count, UrlUtil::Encode(keywords).c_str());
    return buff;
}

void CQQMusicProvider::ParseSearchResult(const std::string& response,
                                         std::vector<DownloadItem>& out) const
{
    out.clear();

    JsonValue root;
    if (!JsonValue::Parse(LyricProviderUtil::StripJsonp(response), root))
        return;

    const JsonValue& list = root["data"]["song"]["list"];
    if (!list.IsArray())
        return;

    for (const JsonValue& song : list.Elements())
    {
        DownloadItem item;
        item.id = song.GetString("songmid");
        if (item.id.empty())
            continue;
        item.title = song.GetString("songname");
        item.album = song.GetString("albumname");
        item.duration = static_cast<int>(song.GetInt("interval", 0)) * 1000;

        long long track = song.GetInt("cdIdx", -1);
        if (track >= 0)
            item.track = static_cast<int>(track);

        const JsonValue& singers = song["singer"];
        if (singers.IsArray())
        {
            for (const JsonValue& singer : singers.Elements())
            {
                std::string name = singer.GetString("name");
                if (name.empty())
                    continue;
                if (!item.artist.empty())
                    item.artist += ';';
                item.artist += name;
            }
        }
        out.push_back(std::move(item));
    }
}

std::string CQQMusicProvider::GetLyricUrl(const std::string& song_id, bool with_translation) const
{
    (void)with_translation;     // 同一个接口同时返回原文和翻译
    return "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=" + song_id
         + "&format=json&nobase64=1";
}

bool CQQMusicProvider::ParseLyric(const std::string& response, bool with_translation,
                                  std::string& lyric_out) const
{
    lyric_out.clear();

    JsonValue root;
    if (!JsonValue::Parse(LyricProviderUtil::StripJsonp(response), root))
        return false;

    std::string lyric = root.GetString("lyric");
    if (lyric.empty())
        return false;
    lyric_out = lyric;

    if (with_translation)
    {
        std::string translation = root.GetString("trans");
        if (!translation.empty())
        {
            if (lyric_out.back() != '\n')
                lyric_out += '\n';
            lyric_out += translation;
        }
    }
    return true;
}

std::string CQQMusicProvider::GetCoverInfoUrl(const std::string& song_id) const
{
    if (song_id.empty())
        return std::string();
    return "https://c.y.qq.com/v8/fcg-bin/fcg_play_single_song.fcg?songmid=" + song_id + "&format=json";
}

std::string CQQMusicProvider::ParseCoverUrl(const std::string& response) const
{
    JsonValue root;
    if (!JsonValue::Parse(LyricProviderUtil::StripJsonp(response), root))
        return std::string();

    const JsonValue& data = root["data"];
    if (!data.IsArray() || data.Size() == 0)
        return std::string();

    // 图片地址不在响应里，需要用专辑的 mid 拼出来
    std::string album_mid = data[0]["album"].GetString("mid");
    if (album_mid.empty())
        return std::string();
    return "http://y.gtimg.cn/music/photo_new/T002R800x800M000" + album_mid + ".jpg";
}

std::vector<std::string> CQQMusicProvider::GetExtraHeaders() const
{
    // 不带 Referer 时 QQ 音乐的接口会返回空数据
    return {
        "Referer: https://y.qq.com/",
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
    };
}

// ============================================================ 公共辅助

namespace LyricProviderUtil
{

void AddLyricTag(std::string& lyric, const std::string& song_id, const std::string& title,
                 const std::string& artist, const std::string& album)
{
    std::string tags;
    if (!song_id.empty())
        tags += "[id:" + song_id + "]\n";

    // 已有非空的同名标签时不再重复添加
    struct { const char* prefix; const char* empty_form; const std::string* value; } items[] = {
        { "[ti:", "[ti:]", &title },
        { "[ar:", "[ar:]", &artist },
        { "[al:", "[al:]", &album },
    };
    for (const auto& item : items)
    {
        if (item.value->empty())
            continue;
        bool has_tag = lyric.find(item.prefix) != std::string::npos
                    && lyric.find(item.empty_form) == std::string::npos;
        if (!has_tag)
            tags += std::string(item.prefix) + *item.value + "]\n";
    }
    lyric = tags + lyric;
}

std::string StripJsonp(const std::string& response)
{
    std::string trimmed = StringUtil::Trimmed(response);
    if (trimmed.empty())
        return trimmed;

    // 已经是纯 JSON
    if (trimmed.front() == '{' || trimmed.front() == '[')
        return trimmed;

    size_t open = trimmed.find('(');
    size_t close = trimmed.find_last_of(')');
    if (open == std::string::npos || close == std::string::npos || close <= open + 1)
        return trimmed;

    // 括号前面必须是合法的回调函数名，否则原样返回，避免误伤
    for (size_t i = 0; i < open; ++i)
    {
        char c = trimmed[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
               || c == '_' || c == '$' || c == '.';
        if (!ok)
            return trimmed;
    }
    return StringUtil::Trimmed(trimmed.substr(open + 1, close - open - 1));
}

std::string MakeSearchKeyword(const std::string& title, const std::string& artist,
                              const std::string& file_name)
{
    std::string keyword;
    if (!artist.empty())
        keyword += artist;
    if (!title.empty())
    {
        if (!keyword.empty())
            keyword += ' ';
        keyword += title;
    }
    if (keyword.empty())
        keyword = file_name;
    return StringUtil::Trimmed(keyword);
}

}   // namespace LyricProviderUtil
