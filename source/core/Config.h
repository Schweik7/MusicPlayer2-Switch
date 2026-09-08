#pragma once
#include <map>
#include <string>

// 简单的 key=value 配置文件（sdmc:/config/MusicPlayer2/config.ini）。
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

    // 双语歌词的排版：false 为单栏（译文排在原文下方），
    // true 为双栏（左原文右译文，同一行左右对齐）
    bool GetLyricTwoColumn() const { return GetBool("lyric_two_column", false); }
    void SetLyricTwoColumn(bool b) { SetBool("lyric_two_column", b); }

    // 拖动歌词时是否连同播放进度一起跳。
    // 关闭时拖动只是翻看，松手几秒后自动滑回当前播放的那句。
    bool GetLyricSeekSync() const { return GetBool("lyric_seek_sync", false); }
    void SetLyricSeekSync(bool b) { SetBool("lyric_seek_sync", b); }

    // 下载完的歌词/封面是否顺手写进音频文件本身。
    // 默认关：这要重写整个文件，得由用户明确同意才动他的音乐。
    bool GetEmbedDownloads() const { return GetBool("embed_downloads", false); }
    void SetEmbedDownloads(bool b) { SetBool("embed_downloads", b); }

    // 歌词区背景。0=不用，1=当前曲目的封面，2=数据目录里的 background.jpg
    enum LyricBackground
    {
        LB_NONE = 0,
        LB_COVER,
        LB_FILE,
        LB_COUNT
    };
    LyricBackground GetLyricBackground() const
    {
        int value = GetInt("lyric_background", LB_NONE);
        if (value < 0 || value >= LB_COUNT)
            value = LB_NONE;
        return static_cast<LyricBackground>(value);
    }
    void SetLyricBackground(LyricBackground value) { SetInt("lyric_background", value); }

    // 沉浸模式：把屏幕上的触摸操作区和底栏提示都收起来，只留封面、曲目信息和歌词。
    // 触摸仍然可用（进度条、拖歌词、点封面），只是不再画那些按钮。
    bool GetImmersive() const { return GetBool("immersive", false); }
    void SetImmersive(bool b) { SetBool("immersive", b); }

    bool GetShowSpectrum() const { return GetBool("show_spectrum", true); }
    void SetShowSpectrum(bool b) { SetBool("show_spectrum", b); }

    int  GetLyricOffset() const { return GetInt("lyric_offset", 0); }
    void SetLyricOffset(int ms) { SetInt("lyric_offset", ms); }

    // 掌机模式下降低刷新率以省电
    bool GetPowerSaving() const { return GetBool("power_saving", true); }
    void SetPowerSaving(bool b) { SetBool("power_saving", b); }

    // 触摸操作开关。放游戏机上容易误触，所以做成可关掉的
    bool GetTouchEnabled() const { return GetBool("touch_enabled", true); }
    void SetTouchEnabled(bool b) { SetBool("touch_enabled", b); }

    // 空闲多少秒后自动调暗屏幕；0 表示不自动调暗
    int  GetDimTimeout() const { return GetInt("dim_timeout", 60); }
    void SetDimTimeout(int seconds) { SetInt("dim_timeout", seconds < 0 ? 0 : seconds); }

private:
    std::map<std::string, std::string> m_values;
};
