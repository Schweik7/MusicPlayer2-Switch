#pragma once
#include <string>
#include <utility>
#include <vector>

// 极简 JSON 解析器。
//
// 桌面版用的是 nlohmann/json，但那是个 956KB 的头文件，对 devkitA64 的构建时间不友好，
// 而且这里只需要"读取网易云/QQ 音乐返回的 JSON"这一种用法。
// 这份实现只做只读解析，用返回值报错而不是抛异常，方便在核心层里做主机端测试。
class JsonValue
{
public:
    enum Type
    {
        T_NULL = 0,
        T_BOOL,
        T_NUMBER,
        T_STRING,
        T_ARRAY,
        T_OBJECT
    };

    JsonValue() = default;

    // 解析失败时 out 被重置为 null，error 里写入位置和原因
    static bool Parse(const std::string& text, JsonValue& out, std::string* error = nullptr);

    Type GetType() const { return m_type; }
    bool IsNull() const { return m_type == T_NULL; }
    bool IsBool() const { return m_type == T_BOOL; }
    bool IsNumber() const { return m_type == T_NUMBER; }
    bool IsString() const { return m_type == T_STRING; }
    bool IsArray() const { return m_type == T_ARRAY; }
    bool IsObject() const { return m_type == T_OBJECT; }

    bool AsBool(bool def = false) const;
    double AsNumber(double def = 0.0) const;
    long long AsInt(long long def = 0) const;
    // 非字符串时返回 def；数字不会被隐式转成字符串
    std::string AsString(const std::string& def = std::string()) const;

    // 数组或对象的元素个数；其它类型返回 0
    size_t Size() const;

    // 越界或类型不符时返回一个静态的 null 值，因此可以安全地链式访问：
    //     json["data"]["song"]["list"][0]["songname"].AsString()
    const JsonValue& operator[](size_t index) const;
    const JsonValue& operator[](const std::string& key) const;
    bool Has(const std::string& key) const;

    // 对象成员按解析顺序保存
    const std::vector<std::pair<std::string, JsonValue>>& Members() const { return m_members; }
    const std::vector<JsonValue>& Elements() const { return m_elements; }

    // 便捷读取：键不存在或类型不符时返回 def
    std::string GetString(const std::string& key, const std::string& def = std::string()) const;
    long long GetInt(const std::string& key, long long def = 0) const;
    double GetNumber(const std::string& key, double def = 0.0) const;
    bool GetBool(const std::string& key, bool def = false) const;

    static const JsonValue& Null();

private:
    class Parser;

    Type m_type{ T_NULL };
    bool m_bool{};
    double m_number{};
    std::string m_string;
    std::vector<JsonValue> m_elements;                              // 数组
    std::vector<std::pair<std::string, JsonValue>> m_members;       // 对象
};
