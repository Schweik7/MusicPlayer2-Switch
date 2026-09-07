#pragma once
#include "SongInfo.h"
#include <string>
#include <vector>

class CPathMapper;

// 读写与桌面版 MusicPlayer2 兼容的播放列表文件。
//
// .playlist 每行格式（见桌面版 Playlist.cpp 的说明）：
//   文件路径|是否为cue音轨|cue起始|cue结束|标题|艺术家|唱片集|音轨号|比特率|流派|年份|注释|cue文件路径
// 普通曲目只写文件路径一列，cue 音轨才写满整行。
class CPlaylistFile
{
public:
    enum Type
    {
        PL_PLAYLIST,        // MusicPlayer2 播放列表
        PL_M3U,             // m3u 播放列表
        PL_M3U8             // m3u8 播放列表
    };

    // path_mapper 可为空；非空时读取的路径会被翻译成 sdmc:/ 形式，写入时翻译回去
    explicit CPlaylistFile(const CPathMapper* path_mapper = nullptr);

    bool LoadFromFile(const std::string& file_path);
    bool SaveToFile(const std::string& file_path, Type type) const;

    const std::vector<SongInfo>& GetPlaylist() const { return m_playlist; }
    std::vector<SongInfo>& GetPlaylist() { return m_playlist; }
    void SetPlaylist(std::vector<SongInfo> songs) { m_playlist = std::move(songs); }

    int  AddSongs(const std::vector<SongInfo>& songs, bool insert_begin = false);
    int  GetSongIndex(const SongInfo& song) const;
    bool IsSongInPlaylist(const SongInfo& song) const { return GetSongIndex(song) >= 0; }
    void RemoveSong(const SongInfo& song);

    static bool IsPlaylistFile(const std::string& file_path);
    static Type TypeFromExtension(const std::string& file_path);

    // 直接从解析文本构造，供单元测试使用（base_dir 用于相对路径展开）
    void ParsePlaylistContent(const std::string& utf8_content, const std::string& base_dir);
    void ParseM3uContent(const std::string& utf8_content, const std::string& base_dir);

private:
    void PushIfValid(SongInfo item, const std::string& base_dir);

    const CPathMapper* m_path_mapper{};
    std::string m_path;
    std::vector<SongInfo> m_playlist;
};
