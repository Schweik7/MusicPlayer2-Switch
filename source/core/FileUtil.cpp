#include "FileUtil.h"
#include "StringUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
// 仅为在开发机上编译核心层单元测试而提供；Switch 目标走 POSIX 分支
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace FileUtil
{

std::string NormalizeSeparators(std::string path)
{
    for (char& c : path)
    {
        if (c == '\\')
            c = '/';
    }
    // 压掉重复的 '/'，但保留 "sdmc:/" 之类前缀里唯一的那个
    std::string out;
    out.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == '/' && !out.empty() && out.back() == '/')
            continue;
        out += path[i];
    }
    return out;
}

static size_t LastSeparator(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    return pos;
}

std::string GetDir(const std::string& file_path)
{
    size_t pos = LastSeparator(file_path);
    if (pos == std::string::npos)
        return std::string();
    if (pos == 0)
        return "/";
    return file_path.substr(0, pos);
}

std::string GetFileName(const std::string& file_path)
{
    size_t pos = LastSeparator(file_path);
    return (pos == std::string::npos) ? file_path : file_path.substr(pos + 1);
}

std::string GetFileNameWithoutExt(const std::string& file_path)
{
    std::string name = GetFileName(file_path);
    size_t pos = name.find_last_of('.');
    return (pos == std::string::npos) ? name : name.substr(0, pos);
}

std::string GetExtension(const std::string& file_path, bool with_dot, bool upper)
{
    std::string name = GetFileName(file_path);
    size_t pos = name.find_last_of('.');
    if (pos == std::string::npos)
        return std::string();
    std::string ext = name.substr(with_dot ? pos : pos + 1);
    if (upper)
    {
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    }
    else
    {
        ext = StringUtil::ToLower(ext);
    }
    return ext;
}

std::string ReplaceExtension(const std::string& file_path, const std::string& new_ext)
{
    std::string ext = GetExtension(file_path, true);
    std::string base = ext.empty() ? file_path : file_path.substr(0, file_path.size() - ext.size());
    if (new_ext.empty())
        return base;
    return base + (new_ext[0] == '.' ? new_ext : "." + new_ext);
}

std::string Combine(const std::string& dir, const std::string& name)
{
    if (dir.empty())
        return name;
    if (dir.back() == '/' || dir.back() == '\\')
        return dir + name;
    return dir + "/" + name;
}

bool IsAbsolute(const std::string& path)
{
    if (path.empty())
        return false;
    if (path[0] == '/')
        return true;
    // "sdmc:/xxx"、"C:\xxx"、"D:/xxx"
    size_t colon = path.find(':');
    return colon != std::string::npos && colon + 1 < path.size()
        && (path[colon + 1] == '/' || path[colon + 1] == '\\');
}

std::string RelativeToAbsolute(const std::string& path, const std::string& base_dir)
{
    if (IsAbsolute(path))
        return NormalizeSeparators(path);

    std::string combined = NormalizeSeparators(Combine(base_dir, path));

    // 就地消解 "." 与 ".."
    std::string prefix;
    std::string rest = combined;
    size_t colon = combined.find(":/");
    if (colon != std::string::npos)
    {
        prefix = combined.substr(0, colon + 2);
        rest = combined.substr(colon + 2);
    }
    else if (!combined.empty() && combined[0] == '/')
    {
        prefix = "/";
        rest = combined.substr(1);
    }

    std::vector<std::string> parts;
    StringUtil::Split(rest, '/', parts, true);
    std::vector<std::string> stack;
    for (const std::string& part : parts)
    {
        if (part == ".")
            continue;
        if (part == "..")
        {
            if (!stack.empty())
                stack.pop_back();
            continue;
        }
        stack.push_back(part);
    }

    std::string result = prefix;
    for (size_t i = 0; i < stack.size(); ++i)
    {
        if (i > 0)
            result += "/";
        result += stack[i];
    }
    return result;
}

bool Exists(const std::string& path)
{
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

bool IsDirectory(const std::string& path)
{
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0)
        return false;
    return (st.st_mode & S_IFDIR) != 0;
}

bool ReadAll(const std::string& path, std::string& content)
{
    content.clear();
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr)
        return false;

    char buffer[8192];
    size_t read_bytes;
    while ((read_bytes = std::fread(buffer, 1, sizeof(buffer), fp)) > 0)
        content.append(buffer, read_bytes);

    bool ok = (std::ferror(fp) == 0);
    std::fclose(fp);
    if (!ok)
        content.clear();
    return ok;
}

bool WriteAll(const std::string& path, const std::string& content)
{
    std::string dir = GetDir(path);
    if (!dir.empty() && !Exists(dir))
        CreateDirRecursive(dir);

    FILE* fp = std::fopen(path.c_str(), "wb");
    if (fp == nullptr)
        return false;
    size_t written = content.empty() ? 0 : std::fwrite(content.data(), 1, content.size(), fp);
    bool ok = (written == content.size());
    std::fclose(fp);
    return ok;
}

bool CreateDirRecursive(const std::string& dir)
{
    std::string normalized = NormalizeSeparators(dir);
    if (normalized.empty())
        return false;

    // 从最短的有效前缀开始逐级创建；跳过 "sdmc:" 这样的挂载点前缀
    size_t start = 0;
    size_t colon = normalized.find(":/");
    if (colon != std::string::npos)
        start = colon + 2;
    else if (normalized[0] == '/')
        start = 1;

    for (size_t i = start; i <= normalized.size(); ++i)
    {
        if (i == normalized.size() || normalized[i] == '/')
        {
            if (i == start)
                continue;
            std::string sub = normalized.substr(0, i);
            if (!Exists(sub))
            {
#ifdef _WIN32
                if (::_mkdir(sub.c_str()) != 0)
                    return false;
#else
                if (::mkdir(sub.c_str(), 0777) != 0)
                    return false;
#endif
            }
        }
    }
    return true;
}

bool ListDir(const std::string& dir, std::vector<DirEntry>& entries)
{
    entries.clear();

#ifdef _WIN32
    WIN32_FIND_DATAA find_data{};
    HANDLE handle = ::FindFirstFileA(Combine(dir, "*").c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    do
    {
        std::string name = find_data.cFileName;
        if (name == "." || name == "..")
            continue;
        DirEntry entry;
        entry.name = name;
        entry.is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entries.push_back(entry);
    } while (::FindNextFileA(handle, &find_data));
    ::FindClose(handle);
#else
    DIR* handle = ::opendir(dir.c_str());
    if (handle == nullptr)
        return false;

    struct dirent* ent;
    while ((ent = ::readdir(handle)) != nullptr)
    {
        std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;

        DirEntry entry;
        entry.name = name;
#ifdef DT_DIR
        if (ent->d_type == DT_DIR)
            entry.is_dir = true;
        else if (ent->d_type == DT_UNKNOWN)
            entry.is_dir = IsDirectory(Combine(dir, name));
#else
        entry.is_dir = IsDirectory(Combine(dir, name));
#endif
        entries.push_back(entry);
    }
    ::closedir(handle);
#endif

    std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.is_dir != b.is_dir)
            return a.is_dir;                                    // 目录排在前面
        return StringUtil::ToLower(a.name) < StringUtil::ToLower(b.name);
    });
    return true;
}

}   // namespace FileUtil
