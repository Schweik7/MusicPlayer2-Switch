#pragma once
#include <string>
#include <vector>
#include <cstdint>

// 全工程内部一律使用 UTF-8 的 std::string；仅在读取桌面版遗留文件时做编码转换
namespace StringUtil
{
    // ---- 编码 ----
    std::string    Utf16ToUtf8(const std::u16string& src);
    std::u16string Utf8ToUtf16(const std::string& src);
    // 把 UTF-8 串解码成码点序列，非法字节以 U+FFFD 替换
    std::vector<char32_t> Utf8ToCodePoints(const std::string& src);
    std::string CodePointToUtf8(char32_t cp);
    // 返回从 pos 开始的一个 UTF-8 字符所占字节数（至少为 1，防止死循环）
    size_t Utf8CharLen(const std::string& src, size_t pos);
    // UTF-8 字符个数（非字节数）
    size_t Utf8Length(const std::string& src);
    // 截取前 n 个 UTF-8 字符
    std::string Utf8Substr(const std::string& src, size_t char_count);

    enum class DetectedEncoding { Utf8, Utf16LE, Utf16BE };
    // 去掉文本开头的 BOM，返回检测到的编码
    DetectedEncoding StripBom(std::string& content);
    // 把任意编码的文件内容统一转成 UTF-8（依据 BOM；无 BOM 时按 UTF-8 处理）
    std::string FileContentToUtf8(std::string content);

    // ---- 通用字符串操作 ----
    void Split(const std::string& src, char delim, std::vector<std::string>& result, bool ignore_empty = true);
    void SplitLine(const std::string& src, std::vector<std::string>& result);
    void Trim(std::string& str);                    // 去掉首尾空白（含全角空格、\r）
    std::string Trimmed(std::string str);
    void CharReplace(std::string& str, char old_ch, char new_ch);
    std::string ToLower(std::string str);
    bool EndsWithNoCase(const std::string& str, const std::string& suffix);
    bool StartsWith(const std::string& str, const std::string& prefix);
    bool IsUrl(const std::string& str);
    int  ToInt(const std::string& str);             // 对齐 _wtoi：跳过空白、允许正负号、遇到非数字停止
}
