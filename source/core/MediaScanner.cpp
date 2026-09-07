#include "MediaScanner.h"
#include "FileUtil.h"
#include "StringUtil.h"

#include <algorithm>

const std::vector<std::string>& CMediaScanner::GetSupportedExtensions()
{
    // SDL2_mixer 在 devkitPro portlibs 中默认带 mpg123 / vorbis / opus / flac / modplug
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

void CMediaScanner::ScanDirectory(const std::string& dir, std::vector<SongInfo>& result,
                                  int max_depth, const std::atomic<bool>* cancel)
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
                ScanDirectory(full_path, result, max_depth < 0 ? -1 : max_depth - 1, cancel);
        }
        else if (IsSupportedAudio(full_path))
        {
            SongInfo song;
            song.file_path = full_path;
            FillTagFromFileName(song);
            result.push_back(std::move(song));
        }
    }
}
