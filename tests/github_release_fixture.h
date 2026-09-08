// 这份夹具是 GitHub API 的真实响应（裁掉无关字段），不是手写的样本。
// 手编的样本测不出真实数据里的转义、CRLF 和中文正文。
static const char* const kGithubReleaseResponse = R"JSONFIX(
{
  "url": "https://api.github.com/repos/Schweik7/MusicPlayer2/releases/384436077",
  "id": 384436077,
  "tag_name": "v0.4.0",
  "target_commitish": "feature/switch-port",
  "name": "Switch 移植版 v0.4.0",
  "draft": false,
  "prerelease": false,
  "created_at": "2026-09-08T03:53:11Z",
  "published_at": "2026-09-08T03:54:03Z",
  "assets": [
    {
      "id": 549822190,
      "name": "MusicPlayer2.nro",
      "content_type": "application/octet-stream",
      "size": 10800541,
      "download_count": 0,
      "browser_download_url": "https://github.com/Schweik7/MusicPlayer2/releases/download/v0.4.0/MusicPlayer2.nro"
    }
  ],
  "body": "Nintendo Switch 移植版首个发布。\n\n桌面版 MusicPlayer2 基于 MFC，无法交叉编译到 Switch，因此界面层与音频层是重写的；\n播放列表、歌词解析、在线下载等核心逻辑与桌面版保持一致，并有主机端单元测试覆盖。\n\n## 安装\n\n把 `MusicPlayer2.nro` 放到 SD 卡的 `/switch/` 目录，从 hbmenu 启动。\n\n首次使用请在「设置 → 默认音乐目录」之前，先用文件浏览界面（`+`）进入你的音乐目录，\n按 `Y` 将其设为默认目录。\n\n## 功能\n\n- 播放 MP3 / OGG / Opus / FLAC / WAV / AIFF / MOD 系列 / MIDI\n- FLAC 走自己接的 libFLAC 解码路径（devkitPro 的 SDL_mixer 未启用 FLAC），\n  进度与时长取自 STREAMINFO，是本版本中最准确的\n- LRC 歌词，含逐字与翻译显示\n- 频谱显示\n- 从网易云 / QQ 音乐下载歌词与封面\n- 触摸操作，可在顶栏一键关闭以防误触\n- 空闲自动调暗屏幕省电\n- 从本 Release 自动检查并安装更新\n\n## 操作\n\n| 输入 | 功能 |\n|---|---|\n| 方向键 ←→ | 上一曲 / 下一曲 |\n| 方向键 ↑ / ↓ | 播放暂停 / 停止 |\n| 左摇杆 ↑↓ | 音量 |\n| 右摇杆 ←→ | 拖动进度 |\n| ZL / ZR | 快退 / 快进 5 秒 |\n| A | 播放 / 暂停 |\n| X / Y | 切换视图 / 播放模式 |\n| − / + | 播放列表 / 文件浏览 |\n| B + Y | 在线下载歌词封面 |\n| B + + | 设置 |\n\n## 已知问题\n\n**SD 卡上含中文等非 ASCII 字符的文件名无法访问。** 这不是本程序的问题：\n第三方 homebrew（ftpd）在同一张卡上同样无法创建或读取这类文件，\n系统返回的目录项里非 ASCII 字符被替换成了 `_` 且无法 stat。目前仍在排查。\n临时办法是把文件名改成 ASCII。\n\n其余：暂不读取音频标签（曲目信息取自文件名）、不支持 cue 分轨。\n"
}
)JSONFIX";
