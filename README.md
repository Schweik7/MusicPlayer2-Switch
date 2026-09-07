# MusicPlayer2 for Nintendo Switch

MusicPlayer2 的 Nintendo Switch homebrew 移植版。

## 这是一次重写，不是编译移植

桌面版 MusicPlayer2 是 MFC 应用（约 12 万行 C++），界面层、音频内核、文件对话框、注册表配置
全部绑定 Win32 API，无法直接交叉编译到 Switch。因此本目录是一个**独立子项目**：

| 层 | 桌面版 | Switch 版 |
| --- | --- | --- |
| 界面 | MFC + 自绘 UIElement + skins | SDL2 自绘，手柄/触摸驱动 |
| 音频内核 | BASS / FFmpeg（`IPlayerCore`） | SDL2_mixer（mpg123 / vorbisidec / opus / modplug / timidity） |
| 字体 | 系统字体 + GDI | Switch 系统共享字体（`plGetSharedFontByType`），自带中日韩字形 |
| 配置 | ini + 注册表 | `sdmc:/switch/MusicPlayer2/config.ini` |
| 网络 | WinINet | libcurl + mbedTLS（libnx 的 socket / nifm） |
| JSON | nlohmann/json | 自带的精简只读解析器（`net/Json.h`） |
| 标签读取 | taglib | 暂未接入，按文件名/播放列表推断（见「尚未实现」） |

**真正被移植过来的是解析逻辑**，位于 `source/core/`，与桌面版保持行为一致：

- `LrcParser` —— 移植自 `MusicPlayer2/Lyric.cpp` 的 `ParseLyricTimeTag` / `DisposeLrc` /
  `NormalizeLyric`。支持标准 LRC、压缩 LRC（一行多个时间标签）、增强 LRC（逐字卡拉OK）、
  ESLyric 尖括号时间轴、`[ti:]/[ar:]/[al:]/[by:]/[offset:]` 元数据、`" / "` 分隔的翻译。
- `PlaylistFile` —— 与桌面版 `.playlist` 格式**双向兼容**，同时支持 m3u / m3u8。
- `PathMapper` —— 桌面版播放列表里存的是 `D:\Music\a.mp3` 这样的 Windows 路径，
  这一层按最长前缀把它们翻译成 `sdmc:/music/a.mp3`，保存时再翻译回去。
  两边可以共用同一份歌单文件。
- `LyricProvider` / `SongMatcher` —— 移植自 `NeteaseLyricDownload.cpp`、
  `QQMusicLyricDownload.cpp` 和 `LyricDownloadCommon.cpp`：网易云/QQ 音乐的接口地址、
  搜索结果解析、`AddLyricTag`，以及基于编辑距离的加权匹配算法（权值与桌面版一致）。

`source/core/` 与 `source/net/`（`HttpClient.cpp` 除外）不依赖 libnx、SDL 或任何平台头文件，
因此可以在开发机上直接编译测试。

## 构建

### 需不需要 WSL2？

**不需要。** devkitPro 提供 Windows 原生安装包（内置 MSYS2 环境），在 Win11 上装完直接就能构建
`.nro`。WSL2 也可以（走 pacman 装同一套包），但没有额外好处，反而多一层文件系统开销。

**唯一要注意的是：`make` 必须在 devkitPro 的 MSYS2 shell 里跑**，不能在 PowerShell 或
Git Bash 里跑 —— Makefile 依赖 `$DEVKITPRO`、devkitPro 版的 `make` 和一系列 POSIX 工具。

### 1. 安装工具链

1. 从 <https://github.com/devkitPro/installer/releases> 下载 `devkitProUpdater-*.exe`；
2. 安装时勾选 **Switch Development**；
3. 安装完成后从开始菜单打开 **MSYS2**（devkitPro 装的那个，通常在 `C:\devkitPro\msys2`）。

### 2. 安装依赖库

在 MSYS2 shell 里执行：

```bash
pacman -S switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-sdl2_mixer \
          switch-mpg123 switch-libvorbis switch-libogg switch-opusfile switch-opus \
          switch-flac switch-libmodplug switch-freetype switch-libpng switch-libjpeg-turbo \
          switch-libwebp switch-bzip2 switch-zlib \
          switch-curl switch-mbedtls
```

最后两个是在线歌词/封面下载需要的。

### 3. 编译

在 MSYS2 shell 里：

```bash
cd /d/0-code/5-cpp/MusicPlayer2/SwitchPort
make
```

也可以直接从 PowerShell 调用封装脚本（它会自动找到 devkitPro 并进 MSYS2 环境）：

```powershell
pwsh SwitchPort/build.ps1            # 增量构建
pwsh SwitchPort/build.ps1 rebuild    # 清理后重建
```

产物是 `MusicPlayer2.nro`（约 10 MB）。

已在以下环境完成干净构建、零警告：

- devkitA64 GCC 16.1.0 + libnx
- SDL2 / SDL2_ttf 2.22.0 / SDL2_image / SDL2_mixer 2.0.4
- curl + mbedTLS

`Makefile` 里的 `LIBS` 顺序来自 `pkg-config --static --libs`，不是照着常见写法猜的。
如果换了 portlibs 版本导致链接失败，用同样的方式重新取一次即可。

### 4. 部署

把 `MusicPlayer2.nro` 复制到 SD 卡的 `/switch/` 目录，用 hbmenu 启动即可。

调试时也可以用 nxlink 直接推送（Switch 上先打开 hbmenu 并按 Y 进入 nxlink 模式）：

```bash
nxlink -s MusicPlayer2.nro
```

## 主机端测试

核心层与网络层都是平台无关的，可以在开发机上编译运行：

```powershell
pwsh SwitchPort/tests/build_host_test.ps1
```

需要 Visual Studio 或 Build Tools（脚本用 vswhere 自动定位）。覆盖范围：

- UTF-8 处理、LRC 各种方言的解析、播放列表格式往返、路径映射、配置读写、含中文的文件名
- JSON 解析（转义、代理对、错误输入、深度限制）、URL 编码
- 网易云/QQ 的搜索结果与歌词解析（用固定的响应样本）、异常响应的容错
- 编辑距离匹配算法
- **完整的下载流程**：通过 `IHttpClient` 注入假实现，验证搜索 → 自动匹配 → 写盘 →
  被 `CLrcParser` 正确读回，以及各条失败路径

不需要真机，也不需要联网。

## 目录结构

```
SwitchPort/
├── Makefile                    devkitPro NRO 构建
├── source/
│   ├── main.cpp                入口；初始化失败时退回控制台报错
│   ├── App.{h,cpp}             主循环、顶栏/底栏/提示条、界面切换
│   ├── Player.{h,cpp}          播放列表、播放模式、歌词加载
│   ├── core/                   ★ 平台无关层，可在 PC 上编译测试
│   │   ├── PlayTime.h          与桌面版 CPlayTime 等价
│   │   ├── StringUtil.{h,cpp}  UTF-8 处理、编码检测、字符串工具
│   │   ├── FileUtil.{h,cpp}    路径与文件 IO（Switch/Windows 双实现）
│   │   ├── SongInfo.{h,cpp}    桌面版 SongInfo 的精简版
│   │   ├── PlaylistFile.{h,cpp} .playlist / m3u / m3u8 读写
│   │   ├── LrcParser.{h,cpp}   ★ 移植自桌面版 Lyric.cpp
│   │   ├── PathMapper.{h,cpp}  Windows 路径 ↔ sdmc 路径
│   │   ├── MediaScanner.{h,cpp} SD 卡音频扫描
│   │   └── Config.{h,cpp}      ini 配置
│   ├── audio/
│   │   ├── AudioEngine.{h,cpp} SDL2_mixer 播放内核
│   │   └── SpectrumAnalyzer.{h,cpp} post-mix 采样 + FFT 频谱
│   ├── net/                    ★ 除 HttpClient 外同样平台无关，可在 PC 上测试
│   │   ├── Json.{h,cpp}        精简只读 JSON 解析器
│   │   ├── UrlUtil.{h,cpp}     百分号编码、URL 扩展名
│   │   ├── SongMatcher.{h,cpp} ★ 编辑距离 + 加权匹配，移植自桌面版
│   │   ├── LyricProvider.{h,cpp} ★ 网易云 / QQ 音乐接口，移植自桌面版
│   │   ├── HttpClient.{h,cpp}  IHttpClient 接口 + libcurl 实现（仅 Switch）
│   │   └── DownloadManager.{h,cpp} 后台线程调度搜索与下载
│   ├── input/
│   │   ├── InputMap.{h,cpp}    libnx HID：按键、长按重复、摇杆、触摸
│   │   └── SoftKeyboard.{h,cpp} 系统软键盘（输入搜索关键词）
│   └── ui/
│       ├── Theme.h             配色与布局常量
│       ├── Renderer.{h,cpp}    SDL2 绘制 + 共享字体回退链 + 文字纹理缓存
│       ├── Screen.h            界面框架
│       ├── PlayerScreen.*      播放界面（封面/进度/歌词/频谱）
│       ├── PlaylistScreen.*    播放列表
│       ├── BrowserScreen.*     SD 卡浏览
│       └── DownloadScreen.*    在线歌词/封面下载
└── tests/                      核心层与网络层的主机端测试
```

## 操作方式

| 按键 | 播放界面 | 列表 / 浏览界面 | 下载界面 |
| --- | --- | --- | --- |
| A | 播放 / 暂停 | 播放选中项 / 进入目录 | 下载选中项 |
| B | （按住 + 其它键，见下） | 返回 / 上级目录 | 返回 |
| X | 切换歌词 ↔ 频谱 | 播放整个目录（浏览界面） | 输入搜索关键词 |
| Y | 切换播放模式 | 设为默认音乐目录（浏览界面） | 切换音乐源 |
| L / R | 上一首 / 下一首 | 上翻页 / 下翻页 | 上翻页 / 下翻页 |
| ZL / ZR | 快退 / 快进 5 秒 | — | 歌词 / 封面开关 |
| 方向键 ↑↓ | 音量 | 移动光标 | 移动光标 |
| 右摇杆 ←→ | 拖动进度 | — | — |
| − | 打开播放列表 | 切换列表 / 播放界面 | 取消进行中的下载 |
| ＋ | 打开文件浏览 | 切换浏览 / 播放界面 | — |
| 触摸 | — | 点击直接播放 / 进入 | 点击直接下载 |

播放界面按住 **B** 的组合键：

| 组合 | 作用 |
| --- | --- |
| B + 方向键 ←→ | 歌词偏移 ∓0.5 秒 |
| B + Y | 打开在线下载界面 |

## 首次使用

1. 把音乐放到 SD 卡（默认目录 `sdmc:/music`）；
2. 启动后按 **＋** 进入文件浏览，找到你的音乐目录，按 **Y** 设为默认目录；
3. 歌词：把同名 `.lrc` 放在音频旁边，或放到同目录的 `lyrics/` 子目录里；
4. 封面：同名 `.jpg/.png`，或目录里的 `cover/folder/front.jpg`。

想复用桌面版的歌单，把 `.playlist` 文件拷到 SD 卡，再编辑
`sdmc:/switch/MusicPlayer2/pathmap.ini` 配置路径映射（首次运行会自动生成带注释的模板）：

```ini
default_music_dir = sdmc:/music

D:\Music\ = sdmc:/music/
E:\ACG\   = sdmc:/music/acg/
```

## 在线歌词与封面下载

在播放界面按 **B + Y** 打开下载界面。默认会用当前曲目的「艺术家 + 标题」作为关键词
（没有标签时用文件名），按 **A** 搜索，选中一项再按 **A** 下载。

- 歌词存成与音频同名的 `.lrc`，封面存成同名的 `.jpg`/`.png`，下载完立即生效，
  不需要重新播放。
- 歌词会带上 `[id:]/[ti:]/[ar:]/[al:]` 标签，格式与桌面版下载的一致。
- 网易云的译文是一份时间轴相同的独立 LRC，追加在原文之后即可 ——
  `CLrcParser` 的同时间轴合并逻辑会自动把它识别成翻译行。
- **ZL / ZR** 分别开关"下载歌词"和"下载封面"，**Y** 在网易云音乐和 QQ 音乐之间切换。

### HTTPS 证书（重要）

devkitPro 的 curl 走 mbedTLS，但**不会**自动使用 Switch 系统的根证书。程序按以下顺序查找
CA 证书包：

1. `sdmc:/switch/MusicPlayer2/cacert.pem`
2. `romfs:/cacert.pem`（打包进 NRO）

**两者都没有时仍然会连接，但不校验服务器证书**，下载界面右上角会显示「未验证证书」。
如果你介意中间人风险，请从 <https://curl.se/ca/cacert.pem> 下载证书包放到上述位置之一。

QQ 音乐的接口是 HTTPS，网易云的是 HTTP，因此没有证书时网易云不受影响。

## 尚未实现

以下是桌面版有、这个移植版还没有的功能，按实现难度排序：

- **音频标签读取**：目前标题/艺术家来自文件名或播放列表。taglib 本身是可移植的，
  接进来即可（devkitPro 无现成包，需要自行交叉编译）。这也会让在线搜索的匹配更准。
- **cue 音轨**：`.playlist` 里的 cue 条目能被正确解析和保留，但 SDL2_mixer 无法在单个
  音频文件内按音轨定位，因此播放时会被当作整轨。
- **音效**：均衡器、混响、变速播放依赖 BASS_FX，SDL2_mixer 没有对应能力。
- **批量下载歌词**：桌面版可以对整个播放列表批量下载，这里一次只处理当前曲目。
- **软件更新检查**。
- **媒体库**：桌面版的 `song_data.dat` 媒体库、听歌统计、多版本管理。
- **歌词编辑**：桌面版的歌词编辑器依赖 Scintilla。

## 支持的音频格式

devkitPro 的 `switch-sdl2_mixer` 是 **2.0.4**，实际编译进去的解码器可以用
`nm libSDL2_mixer.a | grep Mix_MusicInterface_` 查到：

| 格式 | 解码器 | 说明 |
| --- | --- | --- |
| mp3 | mpg123 | |
| ogg / oga | vorbisidec（Tremor） | 整数解码，音质与官方 libvorbis 有极小差异 |
| opus | opusfile | |
| wav / aiff / aif | 内置 | |
| mod / xm / s3m / it | modplug | |
| mid / midi | timidity | 需要 GUS 音色库，见下 |

**不支持 FLAC。** 该包构建时没有启用 FLAC（`music_flac.o` 在归档里但是空的），
所以浏览界面不会列出 `.flac` 文件 —— 列出来只会点开就报错。桌面版靠 BASS 支持 FLAC，
这是本移植版相对桌面版最明显的能力缺口。想要 FLAC 需要自行用启用 FLAC 的选项重新
构建 `switch-sdl2_mixer`，或者接 libFLAC 自己做一路解码送进 `Mix_HookMusic`。

MIDI 用的是 SDL_mixer 内置的 timidity，需要 SD 卡上有 GUS 音色库和 `timidity.cfg`
才能出声，否则 `Mix_LoadMUS` 会失败。

## 已知限制

- **定位精度**：SDL2_mixer 的 `Mix_GetMusicPosition` 对部分格式不可靠，因此播放位置由
  「定位基准 + 自行累计的经过时间」推算。长时间播放可能出现秒级漂移，切歌或定位后归零。
- **时长探测**：`Mix_MusicDuration` 需要 SDL_mixer ≥ 2.6，而 devkitPro 提供的是 **2.0.4**，
  所以这个接口**当前用不上**（代码里有版本守卫，会自动退化）。实际表现是：首次播放时
  进度条没有总时长显示，播完一遍后才能反推出来。m3u 播放列表里的 `#EXTINF` 时长可以
  作为补充来源。
- **m3u 编码**：桌面版写的 `.m3u` 是 ANSI 编码。本移植版按 UTF-8 解释，
  非 ASCII 文件名的 `.m3u` 会乱码 —— 请改用 `.m3u8` 或 `.playlist`。
- **扫描深度**：启动时扫描默认音乐目录限制 3 层，浏览界面的「播放整个目录」只收当前层，
  避免在大容量 SD 卡上长时间卡住。
- **在线接口稳定性**：网易云和 QQ 音乐的接口都是非公开的，随时可能变更或加验证。
  接口地址集中在 `net/LyricProvider.cpp`，解析逻辑有测试样本覆盖，改起来不难。
- **applet 模式内存**：从 hbmenu 以 applet 模式启动时可用内存较少，大量搜索结果加上
  封面图片可能吃紧。建议用「以 applet 覆盖标题启动」的方式获得完整内存。
