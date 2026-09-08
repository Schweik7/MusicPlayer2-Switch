#include "ReleaseInfo.h"
#include "Json.h"

namespace ReleaseInfo
{

bool Parse(const std::string& json_text, const std::string& asset_name, Info& out,
           std::string& error)
{
    out = Info();
    error.clear();

    JsonValue json;
    std::string parse_error;
    if (!JsonValue::Parse(json_text, json, &parse_error))
    {
        error = "无法解析响应内容";
        return false;
    }

    // GitHub 出错时返回的是 {"message": "...."} 这样的对象，
    // 不加判断的话会被当成"没有 tag_name"，错误信息就丢了
    if (!json.Has("tag_name") && json.Has("message"))
    {
        error = json.GetString("message");
        return false;
    }

    out.tag = json.GetString("tag_name");
    if (out.tag.empty())
    {
        error = "响应里没有 tag_name";
        return false;
    }
    out.notes = json.GetString("body");

    const JsonValue& assets = json["assets"];
    for (size_t i = 0; i < assets.Size(); ++i)
    {
        if (assets[i].GetString("name") != asset_name)
            continue;
        out.asset_url = assets[i].GetString("browser_download_url");
        out.asset_size = static_cast<uint64_t>(assets[i].GetInt("size"));
        break;
    }
    return true;
}

}   // namespace ReleaseInfo
