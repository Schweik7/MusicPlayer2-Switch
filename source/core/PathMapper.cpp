#include "PathMapper.h"
#include "FileUtil.h"
#include "StringUtil.h"

namespace
{
    // 统一成 '/' 分隔、小写、且以 '/' 结尾，便于做前缀匹配
    std::string NormalizePrefix(std::string prefix)
    {
        prefix = FileUtil::NormalizeSeparators(prefix);
        if (!prefix.empty() && prefix.back() != '/')
            prefix += '/';
        return prefix;
    }
}

CPathMapper::CPathMapper()
{
}

void CPathMapper::AddRule(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty())
        return;
    Rule rule;
    rule.from = StringUtil::ToLower(NormalizePrefix(from));
    rule.to = NormalizePrefix(to);
    m_rules.push_back(rule);
}

bool CPathMapper::LoadFromFile(const std::string& ini_path)
{
    std::string content;
    if (!FileUtil::ReadAll(ini_path, content))
        return false;
    content = StringUtil::FileContentToUtf8(std::move(content));

    std::vector<std::string> lines;
    StringUtil::SplitLine(content, lines);
    ClearRules();
    for (std::string line : lines)
    {
        StringUtil::Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
            continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = StringUtil::Trimmed(line.substr(0, pos));
        std::string value = StringUtil::Trimmed(line.substr(pos + 1));
        if (key.empty() || value.empty())
            continue;

        if (StringUtil::ToLower(key) == "default_music_dir")
            m_default_music_dir = FileUtil::NormalizeSeparators(value);
        else
            AddRule(key, value);
    }
    return true;
}

bool CPathMapper::SaveToFile(const std::string& ini_path) const
{
    std::string content;
    content += "# MusicPlayer2 for Switch - 路径映射\n";
    content += "# 左边是桌面版播放列表里的 Windows 路径前缀，右边是 SD 卡上的对应目录。\n";
    content += "# 例：D:\\Music\\ = sdmc:/music/\n\n";
    content += "default_music_dir = " + m_default_music_dir + "\n\n";
    for (const Rule& rule : m_rules)
        content += rule.from + " = " + rule.to + "\n";
    return FileUtil::WriteAll(ini_path, content);
}

std::string CPathMapper::ToSwitchPath(const std::string& path) const
{
    if (path.empty() || StringUtil::IsUrl(path))
        return path;

    std::string normalized = FileUtil::NormalizeSeparators(path);
    // 已经是 Switch 本地路径
    if (StringUtil::StartsWith(StringUtil::ToLower(normalized), "sdmc:/") || normalized[0] == '/')
        return normalized;

    // 取最长匹配：规则可能互相嵌套（如 D:\Music\ 与 D:\Music\ACG\），
    // 按声明顺序匹配会让较短的规则抢先命中
    std::string lower = StringUtil::ToLower(normalized);
    const Rule* best = nullptr;
    for (const Rule& rule : m_rules)
    {
        if (StringUtil::StartsWith(lower, rule.from)
            && (best == nullptr || rule.from.size() > best->from.size()))
        {
            best = &rule;
        }
    }
    if (best != nullptr)
        return best->to + normalized.substr(best->from.size());

    // 没有命中规则：去掉 "X:/" 盘符后挂到默认音乐目录下
    size_t colon = normalized.find(":/");
    std::string tail = (colon == std::string::npos) ? normalized : normalized.substr(colon + 2);
    return FileUtil::Combine(m_default_music_dir, tail);
}

std::string CPathMapper::ToDesktopPath(const std::string& path) const
{
    if (path.empty() || StringUtil::IsUrl(path))
        return path;

    std::string normalized = FileUtil::NormalizeSeparators(path);
    // 同样取最长匹配，否则 sdmc:/music/ 会抢在 sdmc:/music/acg/ 之前命中
    std::string lower = StringUtil::ToLower(normalized);
    const Rule* best = nullptr;
    size_t best_len = 0;
    for (const Rule& rule : m_rules)
    {
        std::string to_lower = StringUtil::ToLower(rule.to);
        if (StringUtil::StartsWith(lower, to_lower) && (best == nullptr || to_lower.size() > best_len))
        {
            best = &rule;
            best_len = to_lower.size();
        }
    }
    if (best == nullptr)
        return normalized;

    // rule.from 是小写的，这里只能还原出小写盘符路径；Windows 路径不区分大小写，无碍
    std::string result = best->from + normalized.substr(best_len);
    for (char& c : result)
    {
        if (c == '/')
            c = '\\';
    }
    return result;
}
