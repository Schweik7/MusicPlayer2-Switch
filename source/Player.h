#pragma once
#include "audio/AudioEngine.h"
#include "core/Config.h"
#include "core/LrcParser.h"
#include "core/PathMapper.h"
#include "core/SongInfo.h"

#include <string>
#include <vector>

// 播放控制器：持有播放列表、当前曲目、播放模式和歌词，
// 对应桌面版 CPlayer 中与界面无关的那一部分职责。
class CPlayer
{
public:
    CPlayer();
    ~CPlayer();

    bool Init();
    void Uninit();

    // ---- 播放列表 ----
    void SetPlaylist(std::vector<SongInfo> songs, int play_index = 0, bool auto_play = true);
    const std::vector<SongInfo>& GetPlaylist() const { return m_playlist; }
    int  GetPlaylistSize() const { return static_cast<int>(m_playlist.size()); }
    bool IsPlaylistEmpty() const { return m_playlist.empty(); }

    int  GetCurrentIndex() const { return m_index; }
    const SongInfo& GetCurrentSong() const;
    // 只移动当前曲目指针并载入歌词，不开始播放（用于恢复上次会话）
    void SelectIndex(int index);

    // 载入播放列表文件（.playlist/.m3u/.m3u8）
    bool LoadPlaylistFile(const std::string& file_path, bool auto_play = true);
    bool SavePlaylistFile(const std::string& file_path) const;

    // ---- 播放控制 ----
    bool PlayIndex(int index);
    void PlayOrPause();
    void Stop();
    bool PlayNext(bool by_user = true);     // by_user=false 表示自然播放结束触发
    bool PlayPrevious();

    void SeekRelative(int delta_ms);
    void SeekTo(int ms);

    int  GetPosition() const { return m_audio.GetCurrentPosition(); }
    int  GetLength() const;
    bool IsPlaying() const { return m_audio.IsPlaying(); }
    CAudioEngine::PlayingState GetState() const { return m_audio.GetState(); }

    int  GetVolume() const { return m_audio.GetVolume(); }
    void SetVolume(int volume);
    void AdjustVolume(int delta);

    CConfig::RepeatMode GetRepeatMode() const { return m_repeat_mode; }
    void SetRepeatMode(CConfig::RepeatMode mode);
    void SwitchRepeatMode();                // 在几种模式间循环切换
    static const char* GetRepeatModeName(CConfig::RepeatMode mode);

    // ---- 歌词 ----
    const CLrcParser& GetLyrics() const { return m_lyrics; }
    bool HasLyrics() const { return !m_lyrics.IsEmpty(); }
    void AdjustLyricOffset(int delta_ms);
    int  GetLyricOffset() const { return m_lyrics.GetUserOffset(); }

    CAudioEngine& GetAudio() { return m_audio; }
    CConfig& GetConfig() { return m_config; }
    CPathMapper& GetPathMapper() { return m_path_mapper; }

    // 每帧调用：处理自然播放结束、写回配置
    void Update();

    // 应用配置里的音量、播放模式、上次播放位置
    void ApplyConfig();
    void SaveConfig();

    const std::string& GetLastError() const { return m_last_error; }

    // 配置与数据的存放目录
    static std::string GetDataDir();

private:
    void LoadLyricForCurrentSong();
    // 按当前播放模式决定下一首的下标；返回 -1 表示应当停止
    int  GetNextIndex(bool by_user) const;
    void ReshuffleIfNeeded();

    CAudioEngine m_audio;
    CConfig m_config;
    CPathMapper m_path_mapper;
    CLrcParser m_lyrics;

    std::vector<SongInfo> m_playlist;
    std::vector<int> m_shuffle_order;       // 随机播放的顺序表，保证一轮内不重复
    int m_shuffle_pos{};
    int m_index{ -1 };
    CConfig::RepeatMode m_repeat_mode{ CConfig::RM_LOOP_PLAYLIST };

    std::string m_playlist_path;
    std::string m_last_error;
    SongInfo m_empty_song;

    // 定时保存配置，避免每帧写 SD 卡
    double m_save_timer{};
};
