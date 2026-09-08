#pragma once

// 版本号与 GitHub Release 的 tag 对应：更新检查就是拿 tag_name 和它比。
// 发版时这里和 tag 必须一起改。
#define MP2_SWITCH_VERSION "0.6.3"

#define MP2_SWITCH_REPO_OWNER "Schweik7"
#define MP2_SWITCH_REPO_NAME  "MusicPlayer2"
#define MP2_SWITCH_REPO_URL   "https://github.com/Schweik7/MusicPlayer2"

// Release 里 NRO 资产的文件名，更新时按它在 assets 里查找
#define MP2_SWITCH_ASSET_NAME "MusicPlayer2.nro"

// 备用更新源。GitHub 在部分地区访问不稳定，主源失败时回退到这里。
//
// 返回的 JSON 刻意做成和 GitHub Release API 一样的形状，
// 这样 ReleaseInfo::Parse 一份代码就能解析两边，不必再维护第二个解析器。
//
// 注意 http:// 和 https:// 的区别：走 http 时无法验证服务器身份，
// 路径上的任何人都能把 NRO 换掉。程序会在这种情况下明确提示"未经验证"，
// 换成 https:// 之后自动恢复静默安装（见 CUpdater::DoInstall）。
#define MP2_SWITCH_MIRROR_URL "http://39.99.245.245:8888/latest.json"
