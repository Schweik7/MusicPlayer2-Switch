#pragma once
#include "PlayTime.h"
#include <string>

// 桌面版 SongInfo 的精简可移植版本。
// 只保留 Switch 端播放与显示真正需要的字段，字符串一律为 UTF-8。
struct SongInfo
{
    std::string file_path;      // 绝对路径（已经过 PathMapper 转换为 sdmc:/ 形式）
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string year;
    std::string comment;

    int track{};
    int bitrate{};
    CPlayTime length;           // 时长，未知时为 0

    bool is_cue{};              // Switch 端不播放 cue 音轨，仅用于识别并跳过
    CPlayTime start_pos;
    CPlayTime end_pos;
    std::string cue_file_path;

    bool operator==(const SongInfo& other) const
    {
        return file_path == other.file_path && track == other.track;
    }

    bool IsTagEmpty() const
    {
        return title.empty() && artist.empty() && album.empty();
    }

    // 显示用标题：没有标签时回退到文件名
    std::string GetTitle() const;
    std::string GetArtist() const;
    std::string GetAlbum() const;

    // “艺术家 - 标题”，用于列表与状态栏
    std::string GetDisplayName() const;
};
