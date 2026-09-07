#include "Config.h"
#include "FileUtil.h"
#include "StringUtil.h"

#include <vector>

bool CConfig::Load(const std::string& file_path)
{
    std::string content;
    if (!FileUtil::ReadAll(file_path, content))
        return false;
    content = StringUtil::FileContentToUtf8(std::move(content));

    std::vector<std::string> lines;
    StringUtil::SplitLine(content, lines);
    for (std::string line : lines)
    {
        StringUtil::Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
            continue;
        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = StringUtil::Trimmed(line.substr(0, pos));
        if (key.empty())
            continue;
        m_values[StringUtil::ToLower(key)] = StringUtil::Trimmed(line.substr(pos + 1));
    }
    return true;
}

bool CConfig::Save(const std::string& file_path) const
{
    std::string content = "# MusicPlayer2 for Switch 配置文件\n[general]\n";
    for (const auto& pair : m_values)
        content += pair.first + " = " + pair.second + "\n";
    return FileUtil::WriteAll(file_path, content);
}

std::string CConfig::GetString(const std::string& key, const std::string& def) const
{
    auto iter = m_values.find(StringUtil::ToLower(key));
    if (iter == m_values.end() || iter->second.empty())
        return def;
    return iter->second;
}

int CConfig::GetInt(const std::string& key, int def) const
{
    auto iter = m_values.find(StringUtil::ToLower(key));
    if (iter == m_values.end() || iter->second.empty())
        return def;
    return StringUtil::ToInt(iter->second);
}

bool CConfig::GetBool(const std::string& key, bool def) const
{
    auto iter = m_values.find(StringUtil::ToLower(key));
    if (iter == m_values.end() || iter->second.empty())
        return def;
    std::string value = StringUtil::ToLower(iter->second);
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

void CConfig::SetString(const std::string& key, const std::string& value)
{
    m_values[StringUtil::ToLower(key)] = value;
}

void CConfig::SetInt(const std::string& key, int value)
{
    m_values[StringUtil::ToLower(key)] = std::to_string(value);
}

void CConfig::SetBool(const std::string& key, bool value)
{
    m_values[StringUtil::ToLower(key)] = value ? "1" : "0";
}

CConfig::RepeatMode CConfig::GetRepeatMode() const
{
    int mode = GetInt("repeat_mode", static_cast<int>(RM_LOOP_PLAYLIST));
    if (mode < 0 || mode >= RM_MAX)
        mode = RM_LOOP_PLAYLIST;
    return static_cast<RepeatMode>(mode);
}
