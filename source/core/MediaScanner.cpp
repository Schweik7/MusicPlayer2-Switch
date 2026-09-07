#include "MediaScanner.h"
#include "FileUtil.h"
#include "StringUtil.h"

#include <algorithm>

const std::vector<std::string>& CMediaScanner::GetSupportedExtensions()
{
    // 这份列表对应 devkitPro 的 switch-sdl2_mixer 2.0.4 实际编译进去的解码器。
    // 用 `nm libSDL2_mixer.a | grep Mix_MusicInterface_` 可以查到权威结果：
    //     MPG123 / OGG / Opus / MODPLUG / TIMIDITY / WAV
    //
    // 注意没有 FLAC：music_flac.o 虽然在归档里但是空的，该包构建时没启用 FLAC。
    // 桌面版靠 BASS 支持 FLAC，这里做不到，所以不把 flac 列进来 ——
    // 列出来只会让用户在浏览界面看到一堆点开就报错的文件。
    //
    // mid/midi 走内置的 TIMIDITY，需要 SD 卡上有 GUS 音色库才能出声，详见 README。
    static const std::vector<std::string> exts{
        "mp3", "ogg", "oga", "opus", "wav", "aiff", "aif",
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
