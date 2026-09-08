#pragma once
#include <string>
#include <vector>

// 版本号比较。
//
// 单独拿出来是为了能在开发机上测：比较写反的话，要么永远提示"已是最新"，
// 要么每次启动都提示有新版本，两种都很难在真机上一眼看出来。
namespace VersionUtil
{
    // 把 "v1.2.3" 拆成 {1,2,3}。允许前导的 v/V；
    // 遇到非数字非点的字符就停止（如 "1.2.0-beta" 得到 {1,2,0}）。
    std::vector<int> Parse(const std::string& text);

    // remote 是否比 local 新。位数不同时缺的位按 0 处理（1.2 == 1.2.0）。
    // remote 解析不出任何数字时返回 false —— 宁可不更新，也不要乱更新。
    bool IsNewer(const std::string& remote, const std::string& local);
}
