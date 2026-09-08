#include "Player.h"
#include "core/FileUtil.h"
#include "core/PlaylistFile.h"

#include <algorithm>
#include <chrono>
#include <random>

namespace
{
    std::mt19937& Rng()
    {
        static std::mt19937 rng{ static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()) };
        return rng;
    }
}

CPlayer::CPlayer()
{
}

CPlayer::~CPlayer()
{
    Uninit();
}

std::string CPlayer::GetDataDir()
{
    // homebrew 的惯例是把配置放在 sdmc:/config/<应用名>/ 下，
    // /switch/ 只放 NRO 本身。早期版本写在了 /switch/MusicPlayer2/，
    // 见 MigrateLegacyDataDir。
    return "sdmc:/config/MusicPlayer2";
}

std::string CPlayer::GetLegacyDataDir()
{
    return "sdmc:/switch/MusicPlayer2";
}

void CPlayer::MigrateLegacyDataDir()
{
    const std::string old_dir = GetLegacyDataDir();
    const std::string new_dir = GetDataDir();
    if (!FileUtil::IsDirectory(old_dir))
        return;

    // 逐个文件搬，不整目录移动：目录里可能有别的东西，
    // 而且 Switch 上目录改名本来就不可靠（见 FileUtil::MoveOverwrite）。
    // 只搬我们自己写的那几个，其余留在原地不动。
    static const char* kFiles[] = {
        "config.ini", "pathmap.ini", "cacert.pem", "font.ttf",
    };
    for (const char* name : kFiles)
    {
        const std::string from = FileUtil::Combine(old_dir, name);
        const std::string to = FileUtil::Combine(new_dir, name);
        // 新位置已经有了就不动：那是用户更新过的版本，不该被旧文件盖掉
        if (!FileUtil::Exists(from) || FileUtil::Exists(to))
            continue;
        FileUtil::MoveOverwrite(from, to);
    }
}

bool CPlayer::Init()
{
    std::string data_dir = GetDataDir();
    FileUtil::CreateDirRecursive(data_dir);
    // 目录建好之后再搬，否则搬过去没地方放
    MigrateLegacyDataDir();

    m_config.Load(FileUtil::Combine(data_dir, "config.ini"));

    std::string pathmap_file = FileUtil::Combine(data_dir, "pathmap.ini");
    if (!m_path_mapper.LoadFromFile(pathmap_file))
    {
        // 首次运行：写一份带注释的示例出来，方便用户按需修改
        m_path_mapper.SetDefaultMusicDir(m_config.GetMusicDir());
        m_path_mapper.SaveToFile(pathmap_file);
    }
    m_path_mapper.SetDefaultMusicDir(m_config.GetMusicDir());

    if (!m_audio.Init())
    {
        m_last_error = m_audio.GetLastError();
        return false;
    }

    // 网络是可选的：初始化失败只是用不了在线下载，不该拦住播放器启动
    m_network_ready = m_http.Init();
    if (m_network_ready)
        m_downloader.SetHttpClient(&m_http);

    ApplyConfig();
    return true;
}

void CPlayer::Uninit()
{
    SaveConfig();
    // 必须等下载线程真正退出再关网络栈，否则它可能在 curl 已经清理后还在用它
    m_downloader.WaitForCompletion();
    m_downloader.SetHttpClient(nullptr);
    m_http.Uninit();
    m_network_ready = false;
    m_audio.Uninit();
}

bool CPlayer::IsNetworkReady() const
{
    return m_network_ready && CCurlHttpClient::IsNetworkAvailable();
}

void CPlayer::ApplyConfig()
{
    m_audio.SetVolume(m_config.GetVolume());
    m_repeat_mode = m_config.GetRepeatMode();
    m_lyrics.SetUserOffset(m_config.GetLyricOffset());
}

void CPlayer::SaveConfig()
{
    m_config.SetVolume(m_audio.GetVolume());
    m_config.SetRepeatMode(m_repeat_mode);
    m_config.SetLastPlaylist(m_playlist_path);
    m_config.SetLastIndex(m_index);
    m_config.SetLastPosition(m_audio.GetCurrentPosition());
    m_config.SetLyricOffset(m_lyrics.GetUserOffset());
    m_config.Save(FileUtil::Combine(GetDataDir(), "config.ini"));
}

const SongInfo& CPlayer::GetCurrentSong() const
{
    if (m_index < 0 || m_index >= static_cast<int>(m_playlist.size()))
        return m_empty_song;
    return m_playlist[m_index];
}

void CPlayer::SelectIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_playlist.size()))
        return;
    m_index = index;
    LoadLyricForCurrentSong();
}

void CPlayer::SetPlaylist(std::vector<SongInfo> songs, int play_index, bool auto_play)
{
    m_playlist = std::move(songs);
    m_shuffle_order.clear();
    m_shuffle_pos = 0;
    ReshuffleIfNeeded();

    if (m_playlist.empty())
    {
        m_index = -1;
        m_audio.Close();
        m_lyrics.Clear();
        return;
    }

    if (play_index < 0 || play_index >= static_cast<int>(m_playlist.size()))
        play_index = 0;

    if (auto_play)
    {
        PlayIndex(play_index);
    }
    else
    {
        m_index = play_index;
        LoadLyricForCurrentSong();
    }
}

bool CPlayer::LoadPlaylistFile(const std::string& file_path, bool auto_play)
{
    CPlaylistFile playlist(&m_path_mapper);
    if (!playlist.LoadFromFile(file_path))
    {
        m_last_error = "无法读取播放列表: " + file_path;
        return false;
    }
    m_playlist_path = file_path;
    SetPlaylist(playlist.GetPlaylist(), 0, auto_play);
    return true;
}

bool CPlayer::SavePlaylistFile(const std::string& file_path) const
{
    CPlaylistFile playlist(&m_path_mapper);
    playlist.SetPlaylist(m_playlist);
    return playlist.SaveToFile(file_path, CPlaylistFile::TypeFromExtension(file_path));
}

bool CPlayer::PlayIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_playlist.size()))
        return false;

    m_index = index;
    const SongInfo& song = m_playlist[index];

    if (!m_audio.Open(song.file_path))
    {
        m_last_error = m_audio.GetLastError();
        m_lyrics.Clear();
        return false;
    }

    // 打开成功后把解码器报告的时长回填到列表里，供界面显示
    int length = m_audio.GetSongLength();
    if (length > 0)
        m_playlist[index].length.fromInt(length);

    LoadLyricForCurrentSong();
    return true;
}

void CPlayer::LoadLyricForCurrentSong()
{
    m_lyrics.Clear();
    const SongInfo& song = GetCurrentSong();
    if (song.file_path.empty())
        return;

    std::string lyric_file = CLrcParser::FindLyricFile(song.file_path);
    if (lyric_file.empty())
        return;

    m_lyrics.ParseFile(lyric_file);
    m_lyrics.SetUserOffset(m_config.GetLyricOffset());
}

void CPlayer::PlayOrPause()
{
    if (m_audio.GetState() == CAudioEngine::PS_STOPPED)
    {
        if (m_index >= 0)
            PlayIndex(m_index);
        else if (!m_playlist.empty())
            PlayIndex(0);
        return;
    }
    m_audio.TogglePause();
}

void CPlayer::Stop()
{
    m_audio.Stop();
}

void CPlayer::ReshuffleIfNeeded()
{
    if (m_repeat_mode != CConfig::RM_PLAY_SHUFFLE)
        return;
    if (m_shuffle_order.size() == m_playlist.size() && !m_playlist.empty())
        return;

    m_shuffle_order.resize(m_playlist.size());
    for (size_t i = 0; i < m_shuffle_order.size(); ++i)
        m_shuffle_order[i] = static_cast<int>(i);
    std::shuffle(m_shuffle_order.begin(), m_shuffle_order.end(), Rng());
    m_shuffle_pos = 0;
}

int CPlayer::GetNextIndex(bool by_user) const
{
    if (m_playlist.empty())
        return -1;
    const int count = static_cast<int>(m_playlist.size());

    switch (m_repeat_mode)
    {
    case CConfig::RM_LOOP_TRACK:
        // 单曲循环：自然结束时重播本曲，用户主动按下一首时仍然切歌
        return by_user ? (m_index + 1) % count : m_index;

    case CConfig::RM_PLAY_TRACK:
        // 单曲播放：放完就停
        return by_user ? (m_index + 1) % count : -1;

    case CConfig::RM_PLAY_ORDER:
        // 顺序播放：放到末尾就停，不回到开头
        return (m_index + 1 >= count) ? -1 : m_index + 1;

    case CConfig::RM_PLAY_SHUFFLE:
    {
        if (m_shuffle_order.empty())
            return (m_index + 1) % count;
        int next_pos = (m_shuffle_pos + 1) % static_cast<int>(m_shuffle_order.size());
        return m_shuffle_order[next_pos];
    }

    case CConfig::RM_LOOP_PLAYLIST:
    default:
        return (m_index + 1) % count;
    }
}

bool CPlayer::PlayNext(bool by_user)
{
    ReshuffleIfNeeded();
    int next = GetNextIndex(by_user);
    if (next < 0)
    {
        m_audio.Stop();
        return false;
    }

    if (m_repeat_mode == CConfig::RM_PLAY_SHUFFLE && !m_shuffle_order.empty())
        m_shuffle_pos = (m_shuffle_pos + 1) % static_cast<int>(m_shuffle_order.size());

    // 单曲循环且是自然结束：直接从头播放，不用重新打开文件
    if (next == m_index && !by_user && m_repeat_mode == CConfig::RM_LOOP_TRACK)
    {
        if (m_audio.SetPosition(0))
        {
            m_audio.Play();
            return true;
        }
    }
    return PlayIndex(next);
}

bool CPlayer::PlayPrevious()
{
    if (m_playlist.empty())
        return false;
    const int count = static_cast<int>(m_playlist.size());

    // 播放超过 3 秒时，“上一首”先回到本曲开头，这是播放器的通行做法
    if (m_audio.GetCurrentPosition() > 3000)
    {
        m_audio.SetPosition(0);
        return true;
    }

    if (m_repeat_mode == CConfig::RM_PLAY_SHUFFLE && !m_shuffle_order.empty())
    {
        m_shuffle_pos = (m_shuffle_pos - 1 + static_cast<int>(m_shuffle_order.size()))
                        % static_cast<int>(m_shuffle_order.size());
        return PlayIndex(m_shuffle_order[m_shuffle_pos]);
    }
    return PlayIndex((m_index - 1 + count) % count);
}

void CPlayer::SeekRelative(int delta_ms)
{
    m_audio.Seek(delta_ms);
}

void CPlayer::SeekTo(int ms)
{
    m_audio.SetPosition(ms);
}

int CPlayer::GetLength() const
{
    int length = m_audio.GetSongLength();
    if (length > 0)
        return length;
    // 解码器给不出时长时退回播放列表里记录的值（来自 m3u 的 #EXTINF）
    return GetCurrentSong().length.toInt();
}

void CPlayer::SetVolume(int volume)
{
    m_audio.SetVolume(volume);
}

void CPlayer::AdjustVolume(int delta)
{
    m_audio.AdjustVolume(delta);
}

void CPlayer::SetRepeatMode(CConfig::RepeatMode mode)
{
    m_repeat_mode = mode;
    if (mode == CConfig::RM_PLAY_SHUFFLE)
    {
        m_shuffle_order.clear();
        ReshuffleIfNeeded();
        // 让随机序列从当前曲目开始，避免切到随机模式后立刻跳走
        auto iter = std::find(m_shuffle_order.begin(), m_shuffle_order.end(), m_index);
        if (iter != m_shuffle_order.end())
            m_shuffle_pos = static_cast<int>(iter - m_shuffle_order.begin());
    }
}

void CPlayer::SwitchRepeatMode()
{
    int mode = (static_cast<int>(m_repeat_mode) + 1) % CConfig::RM_MAX;
    SetRepeatMode(static_cast<CConfig::RepeatMode>(mode));
}

const char* CPlayer::GetRepeatModeName(CConfig::RepeatMode mode)
{
    switch (mode)
    {
    case CConfig::RM_PLAY_ORDER:    return "顺序播放";
    case CConfig::RM_PLAY_SHUFFLE:  return "随机播放";
    case CConfig::RM_LOOP_PLAYLIST: return "列表循环";
    case CConfig::RM_LOOP_TRACK:    return "单曲循环";
    case CConfig::RM_PLAY_TRACK:    return "单曲播放";
    default:                        return "未知";
    }
}

void CPlayer::AdjustLyricOffset(int delta_ms)
{
    m_lyrics.SetUserOffset(m_lyrics.GetUserOffset() + delta_ms);
    m_config.SetLyricOffset(m_lyrics.GetUserOffset());
}

void CPlayer::Update()
{
    m_audio.Update();

    if (m_audio.TakeSongFinished())
        PlayNext(false);
}
