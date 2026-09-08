#include "VersionUtil.h"

namespace VersionUtil
{

std::vector<int> Parse(const std::string& text)
{
    std::vector<int> parts;
    size_t i = 0;
    if (i < text.size() && (text[i] == 'v' || text[i] == 'V'))
        ++i;

    int current = 0;
    bool has_digit = false;
    // 多跑一轮（i == size）把最后一段数字收进去，省得循环外再补一次
    for (; i <= text.size(); ++i)
    {
        char c = (i < text.size()) ? text[i] : '.';
        if (c >= '0' && c <= '9')
        {
            current = current * 10 + (c - '0');
            has_digit = true;
        }
        else if (c == '.')
        {
            if (!has_digit)
                break;                      // ".." 或以点开头，到此为止
            parts.push_back(current);
            current = 0;
            has_digit = false;
        }
        else
        {
            if (has_digit)
                parts.push_back(current);   // 形如 "1.2.0-beta"，收下 0 再停
            break;
        }
    }
    return parts;
}

bool IsNewer(const std::string& remote, const std::string& local)
{
    std::vector<int> a = Parse(remote);
    std::vector<int> b = Parse(local);
    if (a.empty())
        return false;

    size_t count = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < count; ++i)
    {
        int x = (i < a.size()) ? a[i] : 0;
        int y = (i < b.size()) ? b[i] : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

}   // namespace VersionUtil
