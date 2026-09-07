#include "Json.h"
#include "../core/StringUtil.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    // 嵌套层数上限，防止畸形输入把递归下降的栈打爆
    const int kMaxDepth = 64;
}

const JsonValue& JsonValue::Null()
{
    static const JsonValue null_value;
    return null_value;
}

bool JsonValue::AsBool(bool def) const
{
    return m_type == T_BOOL ? m_bool : def;
}

double JsonValue::AsNumber(double def) const
{
    return m_type == T_NUMBER ? m_number : def;
}

long long JsonValue::AsInt(long long def) const
{
    return m_type == T_NUMBER ? static_cast<long long>(m_number) : def;
}

std::string JsonValue::AsString(const std::string& def) const
{
    return m_type == T_STRING ? m_string : def;
}

size_t JsonValue::Size() const
{
    if (m_type == T_ARRAY)
        return m_elements.size();
    if (m_type == T_OBJECT)
        return m_members.size();
    return 0;
}

const JsonValue& JsonValue::operator[](size_t index) const
{
    if (m_type != T_ARRAY || index >= m_elements.size())
        return Null();
    return m_elements[index];
}

const JsonValue& JsonValue::operator[](const std::string& key) const
{
    if (m_type != T_OBJECT)
        return Null();
    for (const auto& member : m_members)
    {
        if (member.first == key)
            return member.second;
    }
    return Null();
}

bool JsonValue::Has(const std::string& key) const
{
    if (m_type != T_OBJECT)
        return false;
    for (const auto& member : m_members)
    {
        if (member.first == key)
            return true;
    }
    return false;
}

std::string JsonValue::GetString(const std::string& key, const std::string& def) const
{
    return (*this)[key].AsString(def);
}

long long JsonValue::GetInt(const std::string& key, long long def) const
{
    return (*this)[key].AsInt(def);
}

double JsonValue::GetNumber(const std::string& key, double def) const
{
    return (*this)[key].AsNumber(def);
}

bool JsonValue::GetBool(const std::string& key, bool def) const
{
    return (*this)[key].AsBool(def);
}

// ---------------------------------------------------------------------------

class JsonValue::Parser
{
public:
    Parser(const std::string& text, std::string* error)
        : m_text{ text }, m_error{ error } {}

    bool Run(JsonValue& out)
    {
        SkipWhitespace();
        if (!ParseValue(out, 0))
            return false;
        SkipWhitespace();
        if (m_pos != m_text.size())
        {
            Fail("根值之后存在多余内容");
            return false;
        }
        return true;
    }

private:
    void Fail(const char* reason)
    {
        if (m_error != nullptr && m_error->empty())
        {
            char buff[160];
            std::snprintf(buff, sizeof(buff), "JSON 解析失败（偏移 %zu）：%s", m_pos, reason);
            *m_error = buff;
        }
    }

    bool AtEnd() const { return m_pos >= m_text.size(); }
    char Peek() const { return m_text[m_pos]; }

    void SkipWhitespace()
    {
        while (!AtEnd())
        {
            char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++m_pos;
            else
                break;
        }
    }

    bool Expect(char expected)
    {
        if (AtEnd() || m_text[m_pos] != expected)
        {
            char buff[48];
            std::snprintf(buff, sizeof(buff), "期望字符 '%c'", expected);
            Fail(buff);
            return false;
        }
        ++m_pos;
        return true;
    }

    bool Literal(const char* text, JsonValue& out, JsonValue::Type type, bool bool_value)
    {
        size_t len = std::char_traits<char>::length(text);
        if (m_text.compare(m_pos, len, text) != 0)
        {
            Fail("无法识别的字面量");
            return false;
        }
        m_pos += len;
        out.m_type = type;
        out.m_bool = bool_value;
        return true;
    }

    bool ParseValue(JsonValue& out, int depth)
    {
        if (depth > kMaxDepth)
        {
            Fail("嵌套层数超过上限");
            return false;
        }
        if (AtEnd())
        {
            Fail("内容意外结束");
            return false;
        }

        switch (Peek())
        {
        case '{': return ParseObject(out, depth);
        case '[': return ParseArray(out, depth);
        case '\"':
            out.m_type = T_STRING;
            return ParseString(out.m_string);
        case 't': return Literal("true", out, T_BOOL, true);
        case 'f': return Literal("false", out, T_BOOL, false);
        case 'n': return Literal("null", out, T_NULL, false);
        default:  return ParseNumber(out);
        }
    }

    bool ParseObject(JsonValue& out, int depth)
    {
        if (!Expect('{'))
            return false;
        out.m_type = T_OBJECT;

        SkipWhitespace();
        if (!AtEnd() && Peek() == '}')
        {
            ++m_pos;
            return true;
        }

        while (true)
        {
            SkipWhitespace();
            std::string key;
            if (AtEnd() || Peek() != '\"')
            {
                Fail("对象的键必须是字符串");
                return false;
            }
            if (!ParseString(key))
                return false;

            SkipWhitespace();
            if (!Expect(':'))
                return false;

            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value, depth + 1))
                return false;
            out.m_members.emplace_back(std::move(key), std::move(value));

            SkipWhitespace();
            if (AtEnd())
            {
                Fail("对象没有闭合");
                return false;
            }
            if (Peek() == ',')
            {
                ++m_pos;
                continue;
            }
            if (Peek() == '}')
            {
                ++m_pos;
                return true;
            }
            Fail("对象成员之间期望 ',' 或 '}'");
            return false;
        }
    }

    bool ParseArray(JsonValue& out, int depth)
    {
        if (!Expect('['))
            return false;
        out.m_type = T_ARRAY;

        SkipWhitespace();
        if (!AtEnd() && Peek() == ']')
        {
            ++m_pos;
            return true;
        }

        while (true)
        {
            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value, depth + 1))
                return false;
            out.m_elements.push_back(std::move(value));

            SkipWhitespace();
            if (AtEnd())
            {
                Fail("数组没有闭合");
                return false;
            }
            if (Peek() == ',')
            {
                ++m_pos;
                continue;
            }
            if (Peek() == ']')
            {
                ++m_pos;
                return true;
            }
            Fail("数组元素之间期望 ',' 或 ']'");
            return false;
        }
    }

    // 读取 4 位十六进制；失败返回 false
    bool ReadHex4(unsigned int& value)
    {
        if (m_pos + 4 > m_text.size())
            return false;
        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = m_text[m_pos + i];
            value <<= 4;
            if (c >= '0' && c <= '9')      value |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
            else return false;
        }
        m_pos += 4;
        return true;
    }

    bool ParseString(std::string& out)
    {
        if (!Expect('\"'))
            return false;
        out.clear();

        while (true)
        {
            if (AtEnd())
            {
                Fail("字符串没有闭合");
                return false;
            }
            char c = m_text[m_pos];
            if (c == '\"')
            {
                ++m_pos;
                return true;
            }
            if (c != '\\')
            {
                // 原始字节直接拷贝，UTF-8 序列天然被保留
                out += c;
                ++m_pos;
                continue;
            }

            ++m_pos;                                        // 跳过反斜杠
            if (AtEnd())
            {
                Fail("转义序列没有写完");
                return false;
            }
            char esc = m_text[m_pos++];
            switch (esc)
            {
            case '\"': out += '\"'; break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u':
            {
                unsigned int cp = 0;
                if (!ReadHex4(cp))
                {
                    Fail("\\u 后面不是 4 位十六进制");
                    return false;
                }
                // 代理对：高位后面必须跟一个 \uDCxx 低位，否则按替换字符处理
                if (cp >= 0xD800 && cp <= 0xDBFF)
                {
                    if (m_pos + 1 < m_text.size() && m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u')
                    {
                        size_t saved = m_pos;
                        m_pos += 2;
                        unsigned int low = 0;
                        if (ReadHex4(low) && low >= 0xDC00 && low <= 0xDFFF)
                        {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        else
                        {
                            m_pos = saved;
                            cp = 0xFFFD;
                        }
                    }
                    else
                    {
                        cp = 0xFFFD;
                    }
                }
                else if (cp >= 0xDC00 && cp <= 0xDFFF)
                {
                    cp = 0xFFFD;                            // 落单的低位代理项
                }
                out += StringUtil::CodePointToUtf8(static_cast<char32_t>(cp));
                break;
            }
            default:
                Fail("无法识别的转义字符");
                return false;
            }
        }
    }

    bool ParseNumber(JsonValue& out)
    {
        size_t start = m_pos;
        if (!AtEnd() && (Peek() == '-' || Peek() == '+'))
            ++m_pos;
        bool has_digit = false;
        while (!AtEnd())
        {
            char c = Peek();
            if ((c >= '0' && c <= '9'))
            {
                has_digit = true;
                ++m_pos;
            }
            else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
            {
                ++m_pos;
            }
            else
            {
                break;
            }
        }
        if (!has_digit)
        {
            Fail("不是合法的数字");
            return false;
        }

        std::string token = m_text.substr(start, m_pos - start);
        char* end = nullptr;
        double value = std::strtod(token.c_str(), &end);
        if (end == token.c_str())
        {
            Fail("数字格式错误");
            return false;
        }
        out.m_type = T_NUMBER;
        out.m_number = value;
        return true;
    }

    const std::string& m_text;
    std::string* m_error;
    size_t m_pos{};
};

bool JsonValue::Parse(const std::string& text, JsonValue& out, std::string* error)
{
    out = JsonValue();
    if (error != nullptr)
        error->clear();

    // 有些接口会在 JSON 前面带 BOM
    std::string content = text;
    StringUtil::StripBom(content);

    Parser parser(content, error);
    if (!parser.Run(out))
    {
        out = JsonValue();
        return false;
    }
    return true;
}
