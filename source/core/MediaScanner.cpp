#include "MediaScanner.h"
#include "AudioTag.h"
#include "FileUtil.h"
#include "StringUtil.h"

#include <algorithm>

const std::vector<std::string>& CMediaScanner::GetSupportedExtensions()
{
    // 这份列表对应 devkitPro 的 switch-sdl2_mixer 2.0.4 实际编译进去的解码器。
    // 用 `nm libSDL2_mixer.a | grep Mix_MusicInterface_` 可以查到权威结果：
    //     MPG123 / OGG / Opus / MODPLUG / TIMIDITY / WAV
    //
    // flac 不在这个清单里 —— 该包构建时没启用 FLAC（music_flac.o 是空的）。
    // 我们用 CFlacDecoder 直接调 libFLAC 自己解一路，通过 Mix_HookMusic 送进混音器，
    // 所以这里仍然把 flac 列为可播放。
    //
    // mid/midi 走内置的 TIMIDITY，需要 SD 卡上有 GUS 音色库才能出声，详见 README。
    static const std::vector<std::string> exts{
        "mp3", "ogg", "oga", "opus", "flac", "wav", "aiff", "aif",
        "mod", "xm", "s3m", "it", "mid", "midi"
    };
    return exts;
}

bool CMediaScanner::IsSupportedAudio(const std::string& file_path)
{
    std::string ext = FileUtil::GetExtension(file_path);
    if (ext.empty())
        return false;
    const std::vector<std::string>& exts = GetSupportedExtensions();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

void CMediaScanner::FillTagFromFileName(SongInfo& song)
{
    if (!song.title.empty())
        return;
    std::string name = FileUtil::GetFileNameWithoutExt(song.file_path);
    size_t pos = name.find(" - ");
    if (pos != std::string::npos && pos > 0)
    {
        song.artist = StringUtil::Trimmed(name.substr(0, pos));
        song.title = StringUtil::Trimmed(name.substr(pos + 3));
    }
    else
    {
        song.title = name;
    }
}

void CMediaScanner::FillTag(SongInfo& song, bool read_file_tags)
{
    // 优先读文件内的标签。Switch 的文件系统存不了中文文件名，中文歌传上来
    // 只能改成拼音，靠文件名根本拿不到真正的曲目信息；而标签是文件内部的
    // UTF-8/UTF-16 文本，跟文件系统无关，读出来就是对的。
    if (read_file_tags)
    {
        AudioTag::Tag tag;
        if (AudioTag::Read(song.file_path, tag))
        {
            if (song.title.empty())  song.title = tag.title;
            if (song.artist.empty()) song.artist = tag.artist;
            if (song.album.empty())  song.album = tag.album;
            if (song.genre.empty())  song.genre = tag.genre;
            if (song.year.empty() && tag.year > 0)
                song.year = std::to_string(tag.year);
            if (song.track == 0)
                song.track = tag.track;
        }
    }
    // 标签缺失或读不到时退回文件名
    FillTagFromFileName(song);
}

void CMediaScanner::ScanDirectory(const std::string& dir, std::vector<SongInfo>& result,
                                  int max_depth, const std::atomic<bool>* cancel,
                                  bool read_file_tags)
{
    if (cancel != nullptr && cancel->load())
        return;

    std::vector<FileUtil::DirEntry> entries;
    if (!FileUtil::ListDir(dir, entries))
        return;

    for (const FileUtil::DirEntry& entry : entries)
    {
        if (cancel != nullptr && cancel->load())
            return;

        std::string full_path = FileUtil::Combine(dir, entry.name);
        if (entry.is_dir)
        {
            if (entry.name[0] == '.')                       // 跳过隐藏目录
                continue;
            if (max_depth != 0)
                ScanDirectory(full_path, result, max_depth < 0 ? -1 : max_depth - 1,
                              cancel, read_file_tags);
        }
        else if (IsSupportedAudio(full_path))
        {
            SongInfo song;
            song.file_path = full_path;
            FillTag(song, read_file_tags);
            result.push_back(std::move(song));
        }
    }
}
