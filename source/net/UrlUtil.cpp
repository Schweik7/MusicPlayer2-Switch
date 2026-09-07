#include "UrlUtil.h"
#include "../core/StringUtil.h"

#include <cstdio>

namespace UrlUtil
{

std::string Encode(const std::string& utf8)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(utf8.size() * 3);

    for (unsigned char c : utf8)
    {
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                       || (c >= '0' && c <= '9')
                       || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved)
        {
            out += static_cast<char>(c);
        }
        else
        {
            out += '%';
            out += kHex[(c >> 4) & 0x0F];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

std::string GetExtensionFromUrl(const std::string& url)
{
    // 先砍掉查询串和锚点，再找最后一个点
    size_t end = url.find_first_of("?#");
    std::string path = (end == std::string::npos) ? url : url.substr(0, end);

    size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);

    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size())
        return std::string();

    std::string ext = StringUtil::ToLower(name.substr(dot + 1));
    // 扩展名不可能很长，过长的多半是把路径里的点当成了扩展名
    if (ext.size() > 5)
        return std::string();
    for (char c : ext)
    {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            return std::string();
    }
    return ext;
}

bool IsHttps(const std::string& url)
{
    return StringUtil::StartsWith(StringUtil::ToLower(url), "https://");
}

}   // namespace UrlUtil
