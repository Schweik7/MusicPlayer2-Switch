# MusicPlayer2 for Nintendo Switch

MusicPlayer2 的 Nintendo Switch homebrew 移植版。

## 这是一次重写，不是编译移植

桌面版 MusicPlayer2 是 MFC 应用（约 12 万行 C++），界面层、音频内核、文件对话框、注册表配置
全部绑定 Win32 API，无法直接交叉编译到 Switch。因此本目录是一个**独立子项目**：

| 层 | 桌面版 | Switch 版 |
| --- | --- | --- |
| 界面 | MFC + 自绘 UIElement + skins | SDL2 自绘，手柄/触摸驱动 |
| 音频内核 | BASS / FFmpeg（`IPlayerCore`） | SDL2_mixer（mpg123 / vorbis / opus / FLAC / modplug） |
| 字体 | 系统字体 + GDI | Switch 系统共享字体（`plGetSharedFontByType`），自带中日韩字形 |
| 配置 | ini + 注册表 | `sdmc:/switch/MusicPlayer2/config.ini` |
| 标签读取 | taglib | 暂未接入，按文件名/播放列表推断（见「尚未实现」） |

**真正被移植过来的是解析逻辑**，位于 `source/core/`，与桌面版保持行为一致：

- `LrcParser` —— 移植自 `MusicPlayer2/Lyric.cpp` 的 `ParseLyricTimeTag` / `DisposeLrc` /
  `NormalizeLyric`。支持标准 LRC、压缩 LRC（一行多个时间标签）、增强 LRC（逐字卡拉OK）、
  ESLyric 尖括号时间轴、`[ti:]/[ar:]/[al:]/[by:]/[offset:]` 元数据、`" / "` 分隔的翻译。
- `PlaylistFile` —— 与桌面版 `.playlist` 格式**双向兼容**，同时支持 m3u / m3u8。
- `PathMapper` —— 桌面版播放列表里存的是 `D:\Music\a.mp3` 这样的 Windows 路径，
  这一层按最长前缀把它们翻译成 `sdmc:/music/a.mp3`，保存时再翻译回去。
  两边可以共用同一份歌单文件。

`source/core/` 不依赖 libnx、SDL 或任何平台头文件，因此可以在开发机上直接编译测试。

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
          switch-libwebp switch-bzip2 switch-zlib
```

### 3. 编译

```bash
cd /d/0-code/5-cpp/MusicPlayer2/SwitchPort
make
```

产物是 `MusicPlayer2.nro`。

### 4. 部署

把 `MusicPlayer2.nro` 复制到 SD 卡的 `/switch/` 目录，用 hbmenu 启动即可。

调试时也可以用 nxlink 直接推送（Switch 上先打开 hbmenu 并按 Y 进入 nxlink 模式）：

```bash
nxlink -s MusicPlayer2.nro
```

## 核心层测试

`source/core/` 是平台无关的，可以在开发机上编译运行：

```powershell
pwsh SwitchPort/tests/build_host_test.ps1
```

需要 Visual Studio 或 Build Tools（脚本用 vswhere 自动定位）。测试覆盖 UTF-8 处理、
LRC 各种方言的解析、播放列表格式往返、路径映射和配置读写。

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
│   ├── input/InputMap.{h,cpp}  libnx HID：按键、长按重复、摇杆、触摸
│   └── ui/
│       ├── Theme.h             配色与布局常量
│       ├── Renderer.{h,cpp}    SDL2 绘制 + 共享字体回退链 + 文字纹理缓存
│       ├── Screen.h            界面框架
│       ├── PlayerScreen.*      播放界面（封面/进度/歌词/频谱）
│       ├── PlaylistScreen.*    播放列表
│       └── BrowserScreen.*     SD 卡浏览
└── tests/                      核心层主机端测试
```

## 操作方式

| 按键 | 播放界面 | 列表 / 浏览界面 |
| --- | --- | --- |
| A | 播放 / 暂停 | 播放选中项 / 进入目录 |
| B | （按住 + 方向键左右调歌词偏移） | 返回 / 上级目录 |
| X | 切换歌词 ↔ 频谱 | 播放整个目录（浏览界面） |
| Y | 切换播放模式 | 设为默认音乐目录（浏览界面） |
| L / R | 上一首 / 下一首 | 上翻页 / 下翻页 |
| ZL / ZR | 快退 / 快进 5 秒 | — |
| 方向键 ↑↓ | 音量 | 移动光标 |
| 右摇杆 ←→ | 拖动进度 | — |
| − | 打开播放列表 | 切换列表 / 播放界面 |
| ＋ | 打开文件浏览 | 切换浏览 / 播放界面 |
| 触摸 | — | 点击直接播放 / 进入 |

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

## 尚未实现

以下是桌面版有、这个移植版还没有的功能，按实现难度排序：

- **音频标签读取**：目前标题/艺术家来自文件名或播放列表。taglib 本身是可移植的，
  接进来即可（devkitPro 无现成包，需要自行交叉编译）。
- **cue 音轨**：`.playlist` 里的 cue 条目能被正确解析和保留，但 SDL2_mixer 无法在单个
  音频文件内按音轨定位，因此播放时会被当作整轨。
- **音效**：均衡器、混响、变速播放依赖 BASS_FX，SDL2_mixer 没有对应能力。
- **在线功能**：歌词下载、专辑封面下载、软件更新检查。
- **媒体库**：桌面版的 `song_data.dat` 媒体库、听歌统计、多版本管理。
- **歌词编辑**：桌面版的歌词编辑器依赖 Scintilla。

## 已知限制

- **定位精度**：SDL2_mixer 的 `Mix_GetMusicPosition` 对部分格式不可靠，因此播放位置由
  「定位基准 + 自行累计的经过时间」推算。长时间播放可能出现秒级漂移，切歌或定位后归零。
- **时长探测**：`Mix_MusicDuration` 需要 SDL_mixer ≥ 2.6。低版本下首次播放时进度条无总时长，
  播完一遍后才能拿到。
- **m3u 编码**：桌面版写的 `.m3u` 是 ANSI 编码。本移植版按 UTF-8 解释，
  非 ASCII 文件名的 `.m3u` 会乱码 —— 请改用 `.m3u8` 或 `.playlist`。
- **扫描深度**：启动时扫描默认音乐目录限制 3 层，浏览界面的「播放整个目录」只收当前层，
  避免在大容量 SD 卡上长时间卡住。
