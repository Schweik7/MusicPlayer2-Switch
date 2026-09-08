#pragma once
#include <cstdint>
#include <string>

// GitHub Release 接口的响应解析。
//
// 从 CUpdater 里拆出来，是因为 CUpdater 绑死了 curl 实现、没法在开发机上跑，
// 而"从一坨 JSON 里挑出正确的下载地址"恰恰是最该测的一环：
// 挑错了就会把别的文件下载下来当程序装上去。
namespace ReleaseInfo
{
    struct Info
    {
        std::string tag;            // 形如 "v0.4.0"
        std::string notes;          // release body
        std::string asset_url;      // 指定资产的下载地址；没找到时为空
        uint64_t asset_size{};
    };

    // 解析 /releases/latest 的响应，在 assets 里查找名为 asset_name 的资产。
    // 返回 false 表示 JSON 无法解析或缺少 tag_name，原因写入 error。
    // 注意：找不到目标资产不算失败 —— 那是"有新版本但没带 NRO"，
    // 上层需要把这种情况和"网络出错"区分开来提示。
    bool Parse(const std::string& json_text, const std::string& asset_name, Info& out,
               std::string& error);
}
