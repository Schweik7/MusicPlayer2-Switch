#pragma once
#include <string>
#include <vector>

// 桌面版 MusicPlayer2 保存的播放列表里存的是 Windows 绝对路径（如 "D:\Music\a.mp3"）。
// Switch 上只有 "sdmc:/" 这一个可写挂载点，因此需要一层前缀映射把它们翻译过去。
//
// 映射规则来自 sdmc:/config/MusicPlayer2/pathmap.ini，形如：
//     D:\Music\ = sdmc:/music/
//     E:\ACG\   = sdmc:/music/acg/
// 未命中任何规则时退化为：取盘符后的相对部分拼到默认音乐目录下。
class CPathMapper
{
public:
    struct Rule
    {
        std::string from;       // Windows 侧前缀，已规范化为 '/' 且转小写
        std::string to;         // Switch 侧前缀
    };

    CPathMapper();

    void SetDefaultMusicDir(const std::string& dir) { m_default_music_dir = dir; }
    const std::string& GetDefaultMusicDir() const { return m_default_music_dir; }

    void AddRule(const std::string& from, const std::string& to);
    void ClearRules() { m_rules.clear(); }
    const std::vector<Rule>& GetRules() const { return m_rules; }

    bool LoadFromFile(const std::string& ini_path);
    bool SaveToFile(const std::string& ini_path) const;

    // 把播放列表中的路径翻译成 Switch 上可用的路径。
    // 已经是 sdmc:/ 或 / 开头的路径原样返回；URL 原样返回（Switch 端不播放，仅保留信息）。
    std::string ToSwitchPath(const std::string& path) const;

    // 反向：保存播放列表时把 sdmc:/ 路径写回 Windows 形式，便于两端互通。
    // 没有匹配规则时原样返回 sdmc:/ 路径。
    std::string ToDesktopPath(const std::string& path) const;

private:
    std::vector<Rule> m_rules;
    std::string m_default_music_dir{ "sdmc:/music" };
};
