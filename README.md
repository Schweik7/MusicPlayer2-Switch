# MusicPlayer2 for Nintendo Switch

[MusicPlayer2](https://github.com/zhongyang219/MusicPlayer2) 的 Nintendo Switch homebrew 移植版。

---

## 安装

1. 从 [Releases](https://github.com/Schweik7/MusicPlayer2/releases) 下载 `MusicPlayer2.nro`
2. 放到 SD 卡的 `/switch/` 目录
3. 从 hbmenu 启动

首次使用：按 `＋` 进设置 → **浏览 SD 卡** → 进到你的音乐目录 → 按 `Y` 设为默认音乐目录。
以后启动会自动扫描这个目录。

---

## 功能

### 播放

- **格式**：MP3、FLAC、OGG、Opus、WAV、AIFF、MOD/XM/S3M/IT、MIDI
- **播放模式**：顺序、随机、列表循环、单曲循环、单曲播放
- **三档进度精度**：右摇杆左右快速拖动、ZL/ZR ±5 秒、左摇杆左右 ±1 秒
- **频谱显示**：Hann 窗 + FFT，按对数分组成条
- 退出时记住播放列表、曲目与播放位置

FLAC 走的是自己接的 libFLAC 解码路径（devkitPro 的 SDL_mixer 没有启用 FLAC）。
进度和时长直接取自 STREAMINFO，是本移植版里最准确的格式。

### 曲目信息与封面

- **读取音频标签**：ID3v2.2/2.3/2.4、ID3v1、FLAC 与 Ogg/Opus 的 Vorbis comment
- **内嵌封面**：FLAC 的 PICTURE 块、MP3 的 APIC 帧；没有内嵌封面时回退到同目录的图片文件
- 点击封面可全屏放大

读标签而不是靠文件名，是因为 Switch 的文件系统不支持非 ASCII 文件名（见下方「已知限制」），
中文歌传上去只能改成拼音。标签是文件内部的文本，不受这个限制。

### 歌词

- LRC 歌词，支持**逐字高亮**（卡拉 OK 效果）
- **双语显示**：单栏（译文在原文下方）或双栏（左原文右译文，同步滚动），设置里可切换
- 长句**自动换行**，不会被省略号截断
- 歌词偏移调整（`B` + 方向键左右，每次 0.5 秒）

### 在线下载

- 从**网易云音乐**或 **QQ 音乐**下载歌词与封面
- 自动匹配综合标题、艺术家、专辑、文件名的编辑距离，**并参考时长**——
  同名的不同版本（Live、伴奏、加长版）光看文字分不出来，时长却一目了然
- 也可以手动从搜索结果里挑
- HTTPS 会校验服务器证书（证书包内置在 romfs 里）

### 界面与操作

- **触摸操作**：走带按钮、进度条点击与拖动、列表拖动滚动（带松手惯性）、点列表项直接播放
- **触摸开关**：按下左摇杆或点顶栏按钮即可关闭，避免手持时误触
- 顶栏显示日期时间、播放模式、音量
- 文字用 **Switch 系统共享字体**，自带中日韩字形，不需要往 romfs 里打包字体

### 省电与维护

- **空闲自动调暗屏幕**：走 lbl 服务真的降低背光（这台机器是 LCD，盖一层黑色并不省电）。
  亮度是全局设置，程序会记下你原来的值，唤醒和退出时还原
- **自动更新**：从 GitHub Release 检查并安装新版本。会校验 NRO 魔数、先备份再替换，
  任一步失败都回滚
- **后台扫描音乐库**：几百首歌逐个读标签要十秒左右，扫描放在后台线程，界面立即可用

---

## 操作

### 播放界面

| 输入 | 功能 |
|---|---|
| 方向键 ← → | 上一曲 / 下一曲 |
| 方向键 ↑ / ↓ | 播放暂停 / 停止 |
| 左摇杆 ↑ ↓ | 音量 |
| 左摇杆 ← → | 快退 / 快进 1 秒 |
| 右摇杆 ← → | 拖动进度（快速） |
| ZL / ZR | 快退 / 快进 5 秒 |
| A | 播放 / 暂停 |
| L / R | 上一曲 / 下一曲 |
| X | 切换歌词 / 频谱 |
| Y | 切换播放模式 |
| 按下右摇杆 | 在线下载歌词封面 |
| 按下左摇杆 | 开关触摸操作（任何界面都有效） |
| − | 播放列表 |
| ＋ | 设置 |
| B + 方向键 ← → | 歌词偏移 ∓0.5 秒 |

屏幕左下角的十字按钮与方向键一一对应，直接点也行。

### 列表 / 文件浏览

| 输入 | 功能 |
|---|---|
| 方向键 / 左摇杆 | 移动光标 |
| L / R | 上下翻页 |
| A | 播放 / 打开 |
| B | 返回 / 上级目录 |
| X | 播放整个目录（仅文件浏览） |
| Y | 设为默认音乐目录并载入（仅文件浏览） |
| ＋ | 设置 |

---

## 这是一次重写，不是编译移植

桌面版 MusicPlayer2 是 MFC 应用（约 12 万行 C++），界面层、音频内核、文件对话框、注册表配置
全部绑定 Win32 API，无法直接交叉编译到 Switch。因此本目录是一个**独立子项目**：

| 层 | 桌面版 | Switch 版 |
| --- | --- | --- |
| 界面 | MFC + 自绘 UIElement + skins | SDL2 自绘，手柄/触摸驱动 |
| 音频内核 | BASS / FFmpeg（`IPlayerCore`） | SDL2_mixer + 自己实现的 libFLAC 解码路径 |
| 字体 | 系统字体 + GDI | Switch 系统共享字体（`plGetSharedFontByType`） |
| 配置 | ini + 注册表 | `sdmc:/switch/MusicPlayer2/config.ini` |
| 网络 | WinINet | libcurl + mbedTLS（libnx 的 socket / nifm） |
| JSON | nlohmann/json | 自带的精简只读解析器（`net/Json.h`） |
| 标签读取 | taglib | 自己实现的 `core/AudioTag`，只解析需要的那几个字段 |

**真正被移植过来的是解析逻辑**，位于 `source/core/`，与桌面版保持行为一致：

- `LrcParser` —— 移植自 `MusicPlayer2/Lyric.cpp` 的 `ParseLyricTimeTag` / `DisposeLrc` /
  `NormalizeLyric`。支持标准 LRC、压缩 LRC（一行多个时间标签）、增强 LRC（逐字卡拉 OK）、
  ESLyric 尖括号时间轴、`[ti:]/[ar:]/[al:]/[by:]/[offset:]` 元数据、`" / "` 分隔的翻译。
- `PlaylistFile` —— 与桌面版 `.playlist` 格式**双向兼容**，同时支持 m3u / m3u8。
- `PathMapper` —— 桌面版播放列表里存的是 `D:\Music\a.mp3` 这样的 Windows 路径，
  这一层按最长前缀把它们翻译成 `sdmc:/music/a.mp3`，保存时再翻译回去，
  两边可以共用同一份歌单文件。
- `LyricProvider` / `SongMatcher` —— 移植自 `NeteaseLyricDownload.cpp`、
  `QQMusicLyricDownload.cpp` 和 `LyricDownloadCommon.cpp`，接口地址与匹配权值保持一致
  （另外加了时长这一项）。

---

## 已知限制

### SD 卡上的非 ASCII 文件名无法识别

这是 Switch 文件系统层面的限制，不是本程序的问题——第三方 homebrew（ftpd 等）
在同一张卡上同样无法创建或列出中文名文件。实测表现：

- 非 ASCII 名的文件能创建、能 `stat`、能按原名重新打开，但 `readdir` 从来看不到它们
- 从电脑写入的 `测试提示语.txt`，Switch 读回来的名字是 `.txt`——非 ASCII 字符被抹掉了
- 枚举一个 400 项的目录，含非 ASCII 字节的条目数是 0

GBAStation 之类的传输工具把中文名转成拼音，正是出于同样的原因。

**影响**：文件名只能是 ASCII。但曲目信息改为从标签读取后，列表里显示的仍是正确的中文
标题和艺术家，日常使用基本不受影响。

### 其它

- 不支持 cue 分轨
- MIDI 需要 SD 卡上有 GUS 音色库才能出声
- Ogg/Opus 的内嵌封面（base64 编码在注释里）暂未支持
- 不支持后台播放（需要 sysmodule + overlay 架构，见「后续计划」）

### 更新检查报证书错误？

先校准主机系统时间。时间偏差过大会让所有证书被判为过期，程序会在错误信息里指出这一点。

---

## 从源码构建

需要 [devkitPro](https://devkitpro.org/wiki/Getting_Started) 的 Switch 开发环境：

```
pacman -S switch-dev switch-sdl2 switch-sdl2_mixer switch-sdl2_ttf switch-sdl2_image \
          switch-curl switch-mbedtls switch-flac switch-libjpeg-turbo switch-libpng
```

### Windows

**不需要 WSL2。** devkitPro 的 Windows 安装包自带 MSYS2，直接：

```powershell
pwsh SwitchPort/build.ps1            # 增量构建
pwsh SwitchPort/build.ps1 rebuild    # 全量重建
```

`make` 必须跑在 devkitPro 的 MSYS2 环境里（Makefile 依赖 `$DEVKITPRO`、devkitPro 版的
`make` 和一系列 POSIX 工具），这个脚本只是把它包了一层。

### Linux / macOS

```
cd SwitchPort && make
```

---

## 项目结构与测试

```
SwitchPort/
├── source/
│   ├── core/       平台无关：路径、字符串、播放列表、歌词、标签、配置、扫描
│   ├── net/        平台无关：JSON、URL、匹配、歌词源；Switch 相关：HTTP、更新器
│   ├── audio/      SDL_mixer 封装、libFLAC 解码路径、环形缓冲、频谱
│   ├── ui/         SDL2 渲染与各个界面
│   └── input/      libnx HID 与软键盘
└── tests/          主机端单元测试
```

`core/` 和 `net/` 的大部分不依赖 libnx 和 SDL，可以直接用 MSVC 编译，
因此**不需要真机也不需要联网**就能验证解析逻辑：

```powershell
pwsh SwitchPort/tests/build_host_test.ps1
```

目前 688 条断言，覆盖字符串处理、播放列表读写、LRC 解析、标签解析、JSON、URL 编码、
歌曲匹配、歌词源、下载流程编排、版本比较、音频环形缓冲。

这些测试抓到过多个真实缺陷：ID3v2 里 UTF-16 文本被截掉最后一个字符、路径映射没有做
最长前缀匹配、下载流程把「没有请求」当成「下载成功」、时长参与匹配后阈值判断的漏洞。
另有一类问题只有实机才能暴露（如缺少 `fsdevCommitDevice` 导致写入不落盘），
程序内置了诊断模式：在 `sdmc:/switch/MusicPlayer2/` 放一个 `diag.flag`，
启动后会把文件系统、字体、网络的自检结果写进同目录的 `diag.log`。

---

## 后续计划

- **sysmodule + Tesla overlay**：后台常驻播放，`L + 方向键下` 唤出控制面板。
  overlay 是短命进程，留不住音乐，因此需要拆成「无界面的播放 sysmodule」+「overlay 遥控器」
  两部分（与 sys-tune 同构）。sysmodule 里用不了 SDL，音频层要换成直接调 audout。
- cue 分轨
- 整个播放列表批量下载歌词

---

## 致谢

- 上游项目 [zhongyang219/MusicPlayer2](https://github.com/zhongyang219/MusicPlayer2)
- [devkitPro](https://devkitpro.org/) 与 libnx
