#include "StringUtil.h"
#include <algorithm>
#include <cctype>

namespace StringUtil
{

std::string CodePointToUtf8(char32_t cp)
{
    std::string out;
    if (cp <= 0x7F)
    {
        out += static_cast<char>(cp);
    }
    else if (cp <= 0x7FF)
    {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp <= 0xFFFF)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

std::string Utf16ToUtf8(const std::u16string& src)
{
    std::string out;
    out.reserve(src.size() * 3 / 2);
    for (size_t i = 0; i < src.size(); ++i)
    {
        char32_t cp = src[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < src.size())      // 高代理项
        {
            char32_t low = src[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        out += CodePointToUtf8(cp);
    }
    return out;
}

size_t Utf8CharLen(const std::string& src, size_t pos)
{
    if (pos >= src.size())
        return 0;
    unsigned char c = static_cast<unsigned char>(src[pos]);
    size_t len;
    if      ((c & 0x80) == 0x00) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    else return 1;                                                  // 非法首字节，按 1 字节前进避免死循环
    if (pos + len > src.size())
        return 1;
    for (size_t i = 1; i < len; ++i)                                // 后续字节必须是 10xxxxxx
    {
        if ((static_cast<unsigned char>(src[pos + i]) & 0xC0) != 0x80)
            return 1;
    }
    return len;
}

std::vector<char32_t> Utf8ToCodePoints(const std::string& src)
{
    std::vector<char32_t> out;
    out.reserve(src.size());
    size_t i = 0;
    while (i < src.size())
    {
        size_t len = Utf8CharLen(src, i);
        unsigned char c = static_cast<unsigned char>(src[i]);
        char32_t cp;
        switch (len)
        {
        case 1:
            cp = (c & 0x80) ? 0xFFFD : c;
            break;
        case 2:
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(src[i + 1]) & 0x3Fu);
            break;
        case 3:
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(src[i + 1]) & 0x3Fu) << 6)
                 | (static_cast<unsigned char>(src[i + 2]) & 0x3Fu);
            break;
        default:
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(src[i + 1]) & 0x3Fu) << 12)
                 | ((static_cast<unsigned char>(src[i + 2]) & 0x3Fu) << 6)
                 | (static_cast<unsigned char>(src[i + 3]) & 0x3Fu);
            break;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::u16string Utf8ToUtf16(const std::string& src)
{
    std::u16string out;
    for (char32_t cp : Utf8ToCodePoints(src))
    {
        if (cp <= 0xFFFF)
        {
            out += static_cast<char16_t>(cp);
        }
        else
        {
            cp -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (cp >> 10));
            out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        }
    }
    return out;
}

size_t Utf8Length(const std::string& src)
{
    size_t n = 0, i = 0;
    while (i < src.size())
    {
        i += Utf8CharLen(src, i);
        ++n;
    }
    return n;
}

std::string Utf8Substr(const std::string& src, size_t char_count)
{
    size_t i = 0, n = 0;
    while (i < src.size() && n < char_count)
    {
        i += Utf8CharLen(src, i);
        ++n;
    }
    return src.substr(0, i);
}

DetectedEncoding StripBom(std::string& content)
{
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF
        && static_cast<unsigned char>(content[1]) == 0xBB && static_cast<unsigned char>(content[2]) == 0xBF)
    {
        content.erase(0, 3);
        return DetectedEncoding::Utf8;
    }
    if (content.size() >= 2 && static_cast<unsigned char>(content[0]) == 0xFF
        && static_cast<unsigned char>(content[1]) == 0xFE)
    {
        content.erase(0, 2);
        return DetectedEncoding::Utf16LE;
    }
    if (content.size() >= 2 && static_cast<unsigned char>(content[0]) == 0xFE
        && static_cast<unsigned char>(content[1]) == 0xFF)
    {
        content.erase(0, 2);
        return DetectedEncoding::Utf16BE;
    }
    return DetectedEncoding::Utf8;
}

std::string FileContentToUtf8(std::string content)
{
    DetectedEncoding enc = StripBom(content);
    if (enc == DetectedEncoding::Utf8)
        return content;

    std::u16string u16;
    u16.reserve(content.size() / 2);
    for (size_t i = 0; i + 1 < content.size(); i += 2)
    {
        unsigned char a = static_cast<unsigned char>(content[i]);
        unsigned char b = static_cast<unsigned char>(content[i + 1]);
        u16 += (enc == DetectedEncoding::Utf16LE) ? static_cast<char16_t>(a | (b << 8))
                                                  : static_cast<char16_t>(b | (a << 8));
    }
    return Utf16ToUtf8(u16);
}

void Split(const std::string& src, char delim, std::vector<std::string>& result, bool ignore_empty)
{
    result.clear();
    size_t start = 0;
    while (true)
    {
        size_t pos = src.find(delim, start);
        std::string piece = src.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        if (!ignore_empty || !piece.empty())
            result.push_back(piece);
        if (pos == std::string::npos)
            break;
        start = pos + 1;
    }
}

void SplitLine(const std::string& src, std::vector<std::string>& result)
{
    result.clear();
    size_t start = 0;
    while (start <= src.size())
    {
        size_t pos = src.find('\n', start);
        size_t end = (pos == std::string::npos) ? src.size() : pos;
        size_t len = end - start;
        if (len > 0 && src[start + len - 1] == '\r')                // 兼容 CRLF
            --len;
        result.push_back(src.substr(start, len));
        if (pos == std::string::npos)
            break;
        start = pos + 1;
    }
}

// 需要被当成空白裁掉的字符：ASCII 空白、不换行空格 U+00A0、全角空格 U+3000
static bool IsTrimmableAt(const std::string& s, size_t pos, size_t& char_len)
{
    char_len = Utf8CharLen(s, pos);
    if (char_len == 1)
    {
        unsigned char c = static_cast<unsigned char>(s[pos]);
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    }
    if (char_len == 2)
    {
        return static_cast<unsigned char>(s[pos]) == 0xC2 && static_cast<unsigned char>(s[pos + 1]) == 0xA0;
    }
    if (char_len == 3)
    {
        return static_cast<unsigned char>(s[pos]) == 0xE3 && static_cast<unsigned char>(s[pos + 1]) == 0x80
            && static_cast<unsigned char>(s[pos + 2]) == 0x80;
    }
    return false;
}

void Trim(std::string& str)
{
    size_t begin = 0, char_len = 0;
    while (begin < str.size() && IsTrimmableAt(str, begin, char_len))
        begin += char_len;

    size_t end = str.size();
    while (end > begin)
    {
        size_t last = end - 1;                                      // 回退到最后一个字符的首字节
        while (last > begin && (static_cast<unsigned char>(str[last]) & 0xC0) == 0x80)
            --last;
        if (!IsTrimmableAt(str, last, char_len) || last + char_len != end)
            break;
        end = last;
    }
    str = str.substr(begin, end - begin);
}

std::string Trimmed(std::string str)
{
    Trim(str);
    return str;
}

void CharReplace(std::string& str, char old_ch, char new_ch)
{
    std::replace(str.begin(), str.end(), old_ch, new_ch);
}

std::string ToLower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

bool EndsWithNoCase(const std::string& str, const std::string& suffix)
{
    if (suffix.size() > str.size())
        return false;
    return ToLower(str.substr(str.size() - suffix.size())) == ToLower(suffix);
}

bool StartsWith(const std::string& str, const std::string& prefix)
{
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

bool IsUrl(const std::string& str)
{
    std::string lower = ToLower(str);
    return StartsWith(lower, "http://") || StartsWith(lower, "https://")
        || StartsWith(lower, "ftp://")  || StartsWith(lower, "mms://");
}

int ToInt(const std::string& str)
{
    size_t i = 0;
    while (i < str.size() && (str[i] == ' ' || str[i] == '\t'))
        ++i;
    bool neg = false;
    if (i < str.size() && (str[i] == '+' || str[i] == '-'))
    {
        neg = (str[i] == '-');
        ++i;
    }
    long long value = 0;
    while (i < str.size() && str[i] >= '0' && str[i] <= '9')
    {
        value = value * 10 + (str[i] - '0');
        if (value > 0x7FFFFFFFLL)                                   // 防溢出，行为向 _wtoi 看齐即可
        {
            value = 0x7FFFFFFFLL;
            break;
        }
        ++i;
    }
    return static_cast<int>(neg ? -value : value);
}

}   // namespace StringUtil
