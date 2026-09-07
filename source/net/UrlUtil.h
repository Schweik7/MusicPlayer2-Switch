#pragma once
#include <string>

namespace UrlUtil
{
    // 对查询参数做百分号编码。输入为 UTF-8，未保留字符（A-Za-z0-9-_.~）原样保留，
    // 空格编码为 %20（不是 '+'，网易云和 QQ 的接口都按 RFC 3986 解释）。
    std::string Encode(const std::string& utf8);

    // 从 URL 中取出文件扩展名（不含点，已转小写），用于决定封面存成 .jpg 还是 .png。
    // 拿不到时返回空串。
    std::string GetExtensionFromUrl(const std::string& url);

    // 判断是否是 https，用于在没有 CA 证书时给出有意义的提示
    bool IsHttps(const std::string& url);
}
