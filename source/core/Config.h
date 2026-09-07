#pragma once
#include <map>
#include <string>

// 简单的 key=value 配置文件（sdmc:/switch/MusicPlayer2/config.ini）。
// 不引入第三方 ini 库：Switch 端只需要几十个标量设置项。
class CConfig
{
public:
    enum RepeatMode
    {
        RM_PLAY_ORDER = 0,      // 顺序播放
        RM_PLAY_SHUFFLE,        // 随机播放
        RM_LOOP_PLAYLIST,       // 列表循环
        RM_LOOP_TRACK,          // 单曲循环
        RM_PLAY_TRACK,          // 单曲播放
        RM_MAX
    };

    bool Load(const std::string& file_path);
    bool Save(const std::string& file_path) const;

    // ---- 原始读写 ----
    std::string GetString(const std::string& key, const std::string& def = std::string()) const;
    int  GetInt(const std::string& key, int def = 0) const;
    bool GetBool(const std::string& key, bool def = false) const;
    void SetString(const std::string& key, const std::string& value);
    void SetInt(const std::string& key, int value);
    void SetBool(const std::string& key, bool value);

    // ---- 播放器设置（带默认值的强类型访问）----
    int  GetVolume() const { return GetInt("volume", 80); }
    void SetVolume(int v) { SetInt("volume", v < 0 ? 0 : (v > 100 ? 100 : v)); }

    RepeatMode GetRepeatMode() const;
    void SetRepeatMode(RepeatMode mode) { SetInt("repeat_mode", static_cast<int>(mode)); }

    std::string GetMusicDir() const { return GetString("music_dir", "sdmc:/music"); }
    void SetMusicDir(const std::string& dir) { SetString("music_dir", dir); }

    std::string GetLastPlaylist() const { return GetString("last_playlist"); }
    void SetLastPlaylist(const std::string& p) { SetString("last_playlist", p); }

    int  GetLastIndex() const { return GetInt("last_index", 0); }
    void SetLastIndex(int i) { SetInt("last_index", i); }

    int  GetLastPosition() const { return GetInt("last_position", 0); }
    void SetLastPosition(int ms) { SetInt("last_position", ms); }

    bool GetShowTranslation() const { return GetBool("show_translation", true); }
    void SetShowTranslation(bool b) { SetBool("show_translation", b); }

    bool GetShowSpectrum() const { return GetBool("show_spectrum", true); }
    void SetShowSpectrum(bool b) { SetBool("show_spectrum", b); }

    int  GetLyricOffset() const { return GetInt("lyric_offset", 0); }
    void SetLyricOffset(int ms) { SetInt("lyric_offset", ms); }

    // 掌机模式下降低刷新率以省电
    bool GetPowerSaving() const { return GetBool("power_saving", true); }
    void SetPowerSaving(bool b) { SetBool("power_saving", b); }

private:
    std::map<std::string, std::string> m_values;
};
