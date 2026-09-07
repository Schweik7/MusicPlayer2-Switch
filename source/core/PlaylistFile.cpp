#include "PlaylistFile.h"
#include "PathMapper.h"
#include "FileUtil.h"
#include "StringUtil.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace
{
    // 字段里不允许出现分隔符 '|'
    std::string DeleteInvalidCh(const std::string& str)
    {
        std::string result = str;
        StringUtil::CharReplace(result, '|', '_');
        return result;
    }
}

CPlaylistFile::CPlaylistFile(const CPathMapper* path_mapper)
    : m_path_mapper{ path_mapper }
{
}

bool CPlaylistFile::IsPlaylistFile(const std::string& file_path)
{
    std::string ext = FileUtil::GetExtension(file_path);
    return ext == "playlist" || ext == "m3u" || ext == "m3u8";
}

CPlaylistFile::Type CPlaylistFile::TypeFromExtension(const std::string& file_path)
{
    std::string ext = FileUtil::GetExtension(file_path);
    if (ext == "m3u")
        return PL_M3U;
    if (ext == "m3u8")
        return PL_M3U8;
    return PL_PLAYLIST;
}

bool CPlaylistFile::LoadFromFile(const std::string& file_path)
{
    m_path = file_path;
    m_playlist.clear();

    std::string content;
    if (!FileUtil::ReadAll(file_path, content))
        return false;

    // 桌面版 .playlist/.m3u8 写的是无 BOM 的 UTF-8；.m3u 写的是 ANSI。
    // 这里统一按 UTF-8 解释：ANSI 的非 ASCII 文件名在 Switch 上本来也定位不到，
    // 保留原始字节至少能让 PathMapper 的 ASCII 部分正常工作。
    content = StringUtil::FileContentToUtf8(std::move(content));

    std::string base_dir = FileUtil::GetDir(file_path);
    std::string ext = FileUtil::GetExtension(file_path);
    if (ext == "m3u" || ext == "m3u8")
        ParseM3uContent(content, base_dir);
    else
        ParsePlaylistContent(content, base_dir);
    return true;
}

void CPlaylistFile::PushIfValid(SongInfo item, const std::string& base_dir)
{
    if (item.file_path.empty())
        return;

    bool is_url = StringUtil::IsUrl(item.file_path);
    if (!is_url)
    {
        if (!FileUtil::IsAbsolute(item.file_path))
            item.file_path = FileUtil::RelativeToAbsolute(item.file_path, base_dir);
        if (m_path_mapper != nullptr)
            item.file_path = m_path_mapper->ToSwitchPath(item.file_path);
        else
            item.file_path = FileUtil::NormalizeSeparators(item.file_path);
    }
    m_playlist.push_back(std::move(item));
}

void CPlaylistFile::ParsePlaylistContent(const std::string& utf8_content, const std::string& base_dir)
{
    std::vector<std::string> lines;
    StringUtil::SplitLine(utf8_content, lines);
    for (std::string current_line : lines)
    {
        StringUtil::Trim(current_line);
        // 去掉可能存在的引号
        if (!current_line.empty() && current_line.front() == '\"')
            current_line = current_line.substr(1);
        if (!current_line.empty() && current_line.back() == '\"')
            current_line.pop_back();
        if (current_line.size() <= 3)
            continue;

        std::vector<std::string> fields;
        StringUtil::Split(current_line, '|', fields, false);
        if (fields.empty())
            continue;

        SongInfo item;
        item.file_path = fields[0];
        if (fields.size() >= 2)  item.is_cue = (StringUtil::ToInt(fields[1]) != 0);
        if (fields.size() >= 3)  item.start_pos.fromInt(StringUtil::ToInt(fields[2]));
        if (fields.size() >= 4)  item.end_pos.fromInt(StringUtil::ToInt(fields[3]));
        if (fields.size() >= 5)  item.title = fields[4];
        if (fields.size() >= 6)  item.artist = fields[5];
        if (fields.size() >= 7)  item.album = fields[6];
        if (fields.size() >= 8)  item.track = StringUtil::ToInt(fields[7]);
        if (fields.size() >= 9)  item.bitrate = StringUtil::ToInt(fields[8]);
        if (fields.size() >= 10) item.genre = fields[9];
        if (fields.size() >= 11) item.year = fields[10];
        if (fields.size() >= 12) item.comment = fields[11];
        if (fields.size() >= 13) item.cue_file_path = fields[12];

        PushIfValid(std::move(item), base_dir);
    }
}

void CPlaylistFile::ParseM3uContent(const std::string& utf8_content, const std::string& base_dir)
{
    std::vector<std::string> lines;
    StringUtil::SplitLine(utf8_content, lines);
    std::string track_name;
    int track_length{};
    for (std::string current_line : lines)
    {
        StringUtil::Trim(current_line);
        if (current_line.empty())
            continue;
        if (StringUtil::StartsWith(current_line, "#EXTM3U"))
            continue;

        if (StringUtil::StartsWith(current_line, "#EXTINF"))
        {
            // 形如 "#EXTINF:236,艺术家 - 标题"
            size_t colon = current_line.find(':');
            size_t comma = current_line.rfind(',');
            track_length = (colon == std::string::npos) ? 0
                                                        : StringUtil::ToInt(current_line.substr(colon + 1));
            track_name = (comma == std::string::npos) ? std::string() : current_line.substr(comma + 1);
            continue;
        }
        if (current_line[0] == '#')                                 // 其它注释行（含桌面版写 cue 时的单独 '#'）
            continue;

        SongInfo item;
        item.file_path = current_line;
        item.title = track_name;
        if (track_length > 0)
            item.length.fromInt(track_length * 1000);
        PushIfValid(std::move(item), base_dir);

        track_name.clear();
        track_length = 0;
    }
}

bool CPlaylistFile::SaveToFile(const std::string& file_path, Type type) const
{
    std::string content;

    if (type == PL_PLAYLIST)
    {
        for (const SongInfo& song : m_playlist)
        {
            if (song.file_path.empty())
                continue;
            std::string path = (m_path_mapper != nullptr) ? m_path_mapper->ToDesktopPath(song.file_path)
                                                          : song.file_path;
            content += path;
            if (song.is_cue || StringUtil::IsUrl(song.file_path))
            {
                char buff[512]{};
                std::snprintf(buff, sizeof(buff), "|%d|%d|%d|%s|%s|%s|%d|%d|%s|%s|%s|%s",
                              song.is_cue ? 1 : 0, song.start_pos.toInt(), song.end_pos.toInt(),
                              DeleteInvalidCh(song.title).c_str(), DeleteInvalidCh(song.artist).c_str(),
                              DeleteInvalidCh(song.album).c_str(), song.track, song.bitrate,
                              DeleteInvalidCh(song.genre).c_str(), DeleteInvalidCh(song.year).c_str(),
                              DeleteInvalidCh(song.comment).c_str(), song.cue_file_path.c_str());
                content += buff;
            }
            content += "\n";
        }
    }
    else
    {
        content += "#EXTM3U\n";
        std::set<std::string> saved_cue_path;
        for (const SongInfo& song : m_playlist)
        {
            if (song.file_path.empty())
                continue;
            std::string path = (m_path_mapper != nullptr) ? m_path_mapper->ToDesktopPath(song.file_path)
                                                          : song.file_path;
            if (song.is_cue)
            {
                // 与桌面版一致：cue 音轨只写一次 cue 文件本身
                if (!song.cue_file_path.empty() && saved_cue_path.insert(song.cue_file_path).second)
                    content += "#\n" + song.cue_file_path + "\n";
                continue;
            }
            char buff[512]{};
            std::snprintf(buff, sizeof(buff), "#EXTINF:%d,%s - %s",
                          song.length.toInt() / 1000, song.GetArtist().c_str(), song.GetTitle().c_str());
            content += buff;
            content += "\n";
            content += path;
            content += "\n";
        }
    }

    return FileUtil::WriteAll(file_path, content);
}

int CPlaylistFile::AddSongs(const std::vector<SongInfo>& songs, bool insert_begin)
{
    int added{};
    for (const SongInfo& song : songs)
    {
        if (std::find(m_playlist.begin(), m_playlist.end(), song) != m_playlist.end())
            continue;
        m_playlist.push_back(song);
        ++added;
    }
    if (insert_begin && added > 0)
        std::rotate(m_playlist.rbegin(), m_playlist.rbegin() + added, m_playlist.rend());
    return added;
}

int CPlaylistFile::GetSongIndex(const SongInfo& song) const
{
    auto iter = std::find(m_playlist.begin(), m_playlist.end(), song);
    if (iter == m_playlist.end())
        return -1;
    return static_cast<int>(iter - m_playlist.begin());
}

void CPlaylistFile::RemoveSong(const SongInfo& song)
{
    m_playlist.erase(std::remove(m_playlist.begin(), m_playlist.end(), song), m_playlist.end());
}
