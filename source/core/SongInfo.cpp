#include "SongInfo.h"
#include "FileUtil.h"
#include "Lang.h"

std::string SongInfo::GetTitle() const
{
    if (!title.empty())
        return title;
    if (!file_path.empty())
        return FileUtil::GetFileNameWithoutExt(file_path);
    return T("未知曲目");
}

std::string SongInfo::GetArtist() const
{
    return artist.empty() ? std::string(T("未知艺术家")) : artist;
}

std::string SongInfo::GetAlbum() const
{
    return album.empty() ? std::string(T("未知专辑")) : album;
}

std::string SongInfo::GetDisplayName() const
{
    if (artist.empty())
        return GetTitle();
    return artist + " - " + GetTitle();
}
