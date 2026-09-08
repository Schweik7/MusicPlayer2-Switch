# 系统架构

本文说明 Switch 移植版的分层方式、各模块职责，以及若干关键设计决定背后的原因。
面向要改这份代码的人。用户向的说明见 [README](../README.md)。

---

## 1. 为什么是重写而不是编译移植

桌面版 MusicPlayer2 是 MFC 应用（约 12 万行 C++）。界面层、音频内核、文件对话框、
注册表配置全部绑定 Win32 API，无法交叉编译到 aarch64-none-elf。因此 `SwitchPort/`
是一个**独立子项目**，只共享解析逻辑，不共享任何一行界面或平台代码。

| 层 | 桌面版 | Switch 版 |
| --- | --- | --- |
| 界面 | MFC + 自绘 UIElement + skins | SDL2 自绘，手柄/触摸驱动 |
| 音频内核 | BASS / FFmpeg（`IPlayerCore`） | SDL2_mixer + 自己实现的 libFLAC 解码路径 |
| 字体 | 系统字体 + GDI | Switch 系统共享字体（`plGetSharedFontByType`） |
| 配置 | ini + 注册表 | `sdmc:/config/MusicPlayer2/config.ini` |
| 网络 | WinINet | libcurl + mbedTLS（libnx 的 socket / nifm） |
| JSON | nlohmann/json | 自带的精简只读解析器（`net/Json.h`） |
| 标签读取 | taglib | 自己实现的 `core/AudioTag` |

### 被移植过来的部分

以下模块与桌面版保持**行为一致**，改动只限于把 `CString`/`wstring` 换成 UTF-8 `std::string`：

- **`core/LrcParser`** —— 移植自 `MusicPlayer2/Lyric.cpp` 的 `ParseLyricTimeTag` /
  `DisposeLrc` / `NormalizeLyric`。支持标准 LRC、压缩 LRC（一行多个时间标签）、
  增强 LRC（逐字卡拉 OK）、ESLyric 尖括号时间轴、`[ti:]/[ar:]/[al:]/[by:]/[offset:]`
  元数据、`" / "` 分隔的翻译。
  注意：`split` 里存的是**字节**偏移而非字符偏移，因为时间标签本身全是 ASCII，
  按字节切割不会切坏多字节字符。
- **`core/PlaylistFile`** —— 与桌面版 `.playlist` 格式双向兼容，另支持 m3u/m3u8。
  格式为 `文件路径|是否为cue音轨|cue起始|cue结束|标题|艺术家|唱片集|音轨号|比特率|流派|年份|注释|cue文件路径`；
  普通曲目只存路径，cue/URL 曲目才写完整的竖线分隔行。
- **`net/LyricProvider` / `net/SongMatcher`** —— 移植自 `NeteaseLyricDownload.cpp`、
  `QQMusicLyricDownload.cpp`、`LyricDownloadCommon.cpp`。接口地址与匹配权值保持一致
  （标题 0.4、艺术家 0.4、专辑 0.3、文件名-标题 0.3、文件名-艺术家 0.3、列表序 0.05、
  阈值 0.3），另外加了桌面版没有的时长权重。

---

## 2. 分层

```
main.cpp
  └─ App              主循环、顶栏/底栏/提示条、界面切换、后台任务结果接收
       ├─ Player      播放列表、当前曲目、播放模式、歌词
       │    ├─ audio/AudioEngine     双后端：SDL_mixer / 自己的 FLAC 解码路径
       │    ├─ core/Config           config.ini
       │    ├─ core/PathMapper       Windows 路径 ↔ sdmc: 路径
       │    ├─ core/LrcParser        歌词
       │    └─ net/DownloadManager   在线下载（后台线程）
       ├─ ui/Renderer  SDL2 绘制、字体回退链、文字纹理缓存
       ├─ ui/*Screen   播放 / 列表 / 浏览 / 下载 / 设置
       ├─ input/InputMap  libnx HID + 触摸
       ├─ core/LibraryScanner  音乐库扫描（后台线程）
       ├─ net/Updater          自动更新（后台线程）
       ├─ ScreenDimmer         空闲调暗（lbl 服务）
       └─ Diagnostics          实机自检
```

### 平台无关 / 平台相关的划分

这条线是刻意划的，目的是让**尽可能多的逻辑能在开发机上测**：

| 目录 | 平台无关 | 说明 |
| --- | --- | --- |
| `core/` | 全部 | 只依赖标准库；`FileUtil` 内有 `#ifdef _WIN32` 分支 |
| `net/` | 除 `HttpClient` / `SocketGuard` / `Updater` 外全部 | JSON、URL、匹配、歌词源、`ReleaseInfo` 都可测 |
| `audio/` | 仅 `AudioRingBuffer` | 其余依赖 SDL2 / libFLAC |
| `ui/` `input/` | 无 | 依赖 SDL2 / libnx |

主机端测试（`tests/`）只编译上表中"平台无关"的那些文件。这带来一个实际约束：
**新写的解析类逻辑应当放进 `core/` 或 `net/` 的平台无关部分**，否则它就没法被测。

已经有几次是为此专门做的重构：
- `net/ReleaseInfo` 从 `Updater` 里拆出来，因为 `Updater` 绑死了 curl 实现，链接不了
- `core/VersionUtil` 从 `Updater` 里拆出来
- `ui/ListScroller` 抽出来给三个列表界面共用

---

## 3. 关键设计决定

### 3.1 FLAC 自己解码

devkitPro 的 `switch-sdl2_mixer` 是 2.0.4，构建时**没有启用 FLAC**
（`nm libSDL2_mixer.a | grep Mix_MusicInterface_` 只有 MPG123 / OGG / Opus /
MODPLUG / TIMIDITY / WAV）。所以 FLAC 走单独一条路：

```
解码线程  libFLAC(int32 分声道) → int16 交错 → SDL_AudioStream 重采样到 48kHz 立体声
                                            → 环形缓冲区(~0.7 秒)
音频回调  从环形缓冲区读走，不做任何可能阻塞的事
```

通过 `Mix_HookMusic` 接管音乐流。`AudioEngine` 是双后端（`BK_MIXER` / `BK_FLAC`），
播放/暂停/定位/状态查询按后端分派。

三个要点：

- **解码必须在独立线程**。SD 卡读取延迟有毫秒级尖峰，在音频回调里做文件 IO 必然爆音。
- **seek 用 generation 计数器让在途数据失效**。环形缓冲能存 0.7 秒音频，定位时如果只清
  缓冲不管解码线程手上已解出的那批数据，跳转后会先播放一段旧音频。计数器自增后
  解码线程发现代号变了就丢弃。
- **位置按实际送进混音器的帧数算**，时长直接来自 STREAMINFO。这条路比 SDL_mixer
  那条基于时钟的估算更准，也绕开了 2.0.4 没有 `Mix_MusicDuration` 的问题。
  因此 FLAC 反而是本移植版里进度显示最准确的格式。

`FlacDecoder.h` 里不出现任何 FLAC 类型：`FLAC__Frame` 和 `FLAC__StreamMetadata` 是匿名
结构体的 typedef，**无法前向声明**。解码器句柄用 `void*`，C 回调做成 `.cpp` 内的静态函数，
只把拆好的参数转发给 `OnWrite` / `OnStreamInfo` / `OnDecodeError`。

### 3.2 字体回退链

Switch 系统共享字体按语言分成多个文件，而 SDL_ttf 一个 `TTF_Font` 只对应一个字体文件。
`Renderer` 因此维护一条回退链（标准 → 简中 → 扩展简中 → 繁中 → 韩文 → 任天堂扩展），
绘制时**按码点把字符串切成若干段**，每段用第一个能提供该字形的字体画。

好处是不用往 romfs 里打包字体（能省好几 MB），中日韩字形天然可用。
实测覆盖情况见诊断日志的「系统共享字体覆盖探测」一节。

**回退链是按需建的。** 六档字号 × 六个字体 = 36 次 `TTF_OpenFontRW`，
全放在启动路径上要好几秒，而那几秒屏幕是黑的。`LoadFonts` 现在只做 `plInitialize`
并把字体数据的地址记下来（很便宜），真正的解析交给 `EnsureChain`，
用到哪一档才建哪一档——一次会话里很多字号根本用不到。

用户想换字体的话，把 `font.ttf` 放进数据目录即可，它会被插到回退链最前面，
系统字体留在后面兜底（自带字体缺哪个字形就自动往后找，不会出豆腐块）。
**刻意不打包进 NRO**：好看的中文字体动辄二三十兆，而 NRO 是整个读进内存才启动的，
打进去等于给每次冷启动加上几秒黑屏，正好和上面那件事对着干。

### 3.3 用标签而不是文件名

Switch 的文件系统不支持非 ASCII 文件名（见第 4 节），中文歌传上去只能改成拼音。
`core/AudioTag` 因此直接从文件里读标题/艺术家/专辑：标签是文件内部的 UTF-8/UTF-16 文本，
与文件系统无关。

不引入 taglib：它体积大、构建慢，而这里只需要四五个字段。自己实现的部分包括
ID3v2.2/2.3/2.4、ID3v1、FLAC 与 Ogg/Opus 的 Vorbis comment、FLAC PICTURE 块、ID3 APIC 帧。

两处按现实而非规范的取舍：

- **编码标为 ISO-8859-1、内容其实是 UTF-8 的帧很常见**，所以按内容判断而不是照标准。
  （也有软件塞 GBK，那种情况认不出来会得到乱码；要正确处理得带一张 GBK 映射表，不划算。）
- **标签体可能有几 MB 内嵌封面**，所以先读 32KB、不够再读大块。否则扫描一个音乐库
  要读几百 MB。

### 3.4 后台线程

三处用了后台线程，模式一致（`std::thread` + `mutex` 保护的状态快照 + `atomic<bool>` 取消标志
+ UI 线程每帧 `Poll()`）：

| 模块 | 为什么 |
| --- | --- |
| `net/DownloadManager` | 网络请求可能几秒才回来 |
| `net/Updater` | 同上，另有 10MB 下载 |
| `core/LibraryScanner` | 647 首逐个读标签约 10 秒 |

**析构顺序有硬性要求**：这些线程都要在它们用到的资源被拆掉之前收掉。
`CApp::Uninit` 里先 `m_scanner.WaitForCompletion()` 和 `m_updater.WaitForCompletion()`，
再 `m_player.Uninit()`（后者会 `curl_global_cleanup`）。顺序写反会让工作线程在 curl
已被清理后继续使用它。

`CPlayer` 里 `m_downloader` 持有 `m_http` 的裸指针，因此**成员声明顺序是有意为之的**：
`m_http` 必须声明在 `m_downloader` 之前，才能后于它析构。

### 3.5 自己算时长

devkitPro 带的 SDL_mixer 是 2.0.4，没有 `Mix_MusicDuration`（2.6.0 才有的接口）。
除 FLAC（走自己的解码器，能从 STREAMINFO 直接读总采样数）以外的格式全都拿不到时长，
表现是进度条右侧永远 `-:--`，而且**没有总长度就无法按比例定位**，触摸拖动进度条也用不了。

`core/AudioDuration` 因此自己从文件头算：MP3 读 Xing/Info/VBRI 帧里的总帧数
（没有就按码率和音频字节数估）、WAV 用 fmt 的字节率除 data 长度、
Ogg/Opus 取末页的 granule position。全是偏移量计算，不解码。

一个实机上才暴露的坑：内嵌封面能把 ID3v2 撑到几百 KB，第一个音频帧根本不在
头部缓冲里。所以 MP3 这条路分两步——先读 10 字节拿到标签长度，
再按那个偏移直接读音频段。合成夹具没抓到这个，是拿用户的真实音乐文件跑出来的。

### 3.6 写标签不需要容器库

下载来的封面和歌词能写回音频文件本身，这件事**不需要 ffmpeg 或 taglib**：
封面和歌词都存在文件开头的标签区里，重写标签区、把原来的音频数据原样接在后面就完了。
解码是重活，改标签不是。`core/TagWriter` 处理 MP3（ID3v2 的 APIC / USLT 帧）
和 FLAC（PICTURE 块 / VORBIS_COMMENT）。

Ogg 和 Opus 不支持：它们的注释头包在 Ogg 页里，改一个字段要重新分页并重算每页的 CRC，
那才是真的需要一个容器库。

写入策略是**先完整写出临时文件，确认无误再顶替原文件**，绝不原地改写——
中途失败会毁掉用户的音乐，而这些文件多半没有备份。校验包括：
新文件必须比标签区长（否则音频没搬过去），以及走 `MoveOverwrite` 那条替换路径。

这块的测试给得比别处密，还额外做了一件事：用 `AudioTag` 把写出来的东西再读一遍。
两边是各自独立实现的，能互相校验。

### 3.7 自动更新分两步：下载，然后在启动时替换

实机上**替换正在运行的 NRO 一直失败**。原来的做法是下载完当场换：先把自己
改名成 `.bak`，再把新文件挪过去，失败了再回滚。问题在于那一小段窗口里
程序位置上是空的——回滚要是也失败，用户手上就没有能启动的 NRO 了。
为一个从没成功过的操作冒这个险不值得。

现在改成：运行时只下载到 `<self>.nro.new` 并校验，**不碰**现有的 NRO；
下次启动时 `CUpdater::ApplyPendingUpdate` 再换（那时这一份镜像早已读进内存，
覆盖磁盘上的文件不影响当前进程）。启动时也换不动的话文件仍然留着，
手动改个名就能用，界面上会把路径写出来。

因此界面上必须在**按下下载之前**就说明"下载后需重启才能生效"——
不然用户会以为按完就装好了。

### 3.8 自动更新的其它保守处理

更新下载来的东西会被当程序执行，因此几处刻意保守：

- **强制校验服务器证书**。没有可用 CA 证书包时直接拒绝更新，而不是降级成不验证。
  （普通的歌词/封面下载在没有证书包时会继续连接，但会把"未验证"状态显示给用户。）
- **校验 NRO 魔数**（文件偏移 0x10 处的 `NRO0`），挡住"下到一个 HTML 错误页
  然后把它装上去"。
- **先备份再替换，任一步失败都回滚**。改名和复制都试——`rename` 在实机上会失败。

### 3.9 每个功能都要有两条路

**手柄能做的事，触摸也要能做；反过来也一样。** 这条不是锦上添花——
用户可能把触摸关了（手持时容易误触），也可能整场只用手指。
任何只挂在一种输入上的功能，对另一半用户就是不存在的。

这条规则是被漏掉之后补上的：设置界面只有 B 键能返回，
纯触摸操作进去就出不来了。同一批漏掉的还有音量（只有左摇杆）、
文件浏览的"播放整个目录 / 设为音乐目录"（只有 X / Y）、
下载界面的搜索和几个开关（只有 X / Y / ZL / ZR / ＋）。

落实的办法：

- **返回统一走 `CScreen::GoBack`**。B 键和顶栏的返回按钮调同一个方法，
  各界面只描述一次"上一层是什么"（设置的关于页退回列表，文件浏览退回上级目录）。
  两条路径不会再走偏。
- **顶栏是触摸的兜底入口**。返回、设置、列表、播放模式、音量、触摸开关都在那儿，
  凡是只能靠某个手柄键触发的全局功能，都该在顶栏有个位置。
- **各界面的开关抽成方法**，键盘分支和触摸分支调同一个
  （见 `CDownloadScreen::ToggleLyric` 那一组），改一边忘一边的机会就没了。
- **反向也要补**：看大封面原本只有点封面这一条路，于是把封面加进了 X 的视图循环；
  翻看歌词原本只能拖，于是右摇杆上下也能翻——它俩共用
  `FinishLyricBrowse` 收尾，松手和摇杆回中是同一件事。

加新功能时先问一句：另一半用户怎么用？

### 3.10 触摸与绘制共用同一份坐标

`ui/Theme.h` 里的 `Rect` 同时用于绘制和命中判定，布局常量写在各界面的匿名命名空间里。
这是为了避免"画一套、点另一套"——这类 bug 在真机上极难发现。

顶栏那几个按钮的矩形是在 `DrawHeader` 里按文字宽度算出来的，供**下一帧**的触摸判定使用；
首帧是空矩形，不会误命中。

---

## 4. 平台约束（实机测出来的）

这一节记录的都是踩过的坑，都有诊断数据支撑。

### 4.1 非 ASCII 文件名不可用

**结论：SD 卡上的文件名只能是 ASCII。** 这是 Switch 文件系统层面的限制，
第三方 homebrew（ftpd 等）同样如此。

实测证据：

- 非 ASCII 名的文件能创建、能 `stat`、能按原名重新打开，但 `readdir` **从来看不到它们**
  （连建 5 个之后目录里仍然只有那个 ASCII 的）
- 从电脑写入的 `测试提示语.txt`，Switch 读回来的名字是 `.txt`——非 ASCII 字符被抹掉了
- 枚举一个 400 项的真实目录，含非 ASCII 字节的条目数是 **0**

GBAStation 之类的传输工具把中文名转成拼音，正是出于同样的原因。
应对方式见 3.3。

### 4.2 写文件后必须 `fsdevCommitDevice`

libnx 的原话：

> This should be used after each file-close where file-writing was done.
> **This is not used automatically at device unmount.**

不提交的话数据只停在 FS 服务的缓存里，SD 卡上的目录项元数据不会落盘——表现为
文件列得出来、但大小是 0、权限全空、`stat` 不到。

发现过程值得一提：一开始以为这是中文文件名的问题，直到发现纯 ASCII 名的 `diag.log`
也是同样症状，才意识到规律不是文件名而是**谁写的**（ftpd 写的都正常，我们程序写的都坏）。

`FileUtil::WriteAll` / `CreateDirRecursive` / `CopyFileTo` 内部已经提交；
直接用 `FILE*` 的地方（诊断日志、下载到文件、更新器）需要自己调。
诊断日志按 20 行攒一批提交——提交要刷整个 FAT，每行都提交太贵。

### 4.3 `rename` 不可靠

实机上 `std::rename` 会失败（用户遇到过更新时"无法备份当前版本"）。
凡是要移动文件的地方都要准备复制这条退路，见 `FileUtil::MoveOverwrite`
（先试 `rename`，失败退回"复制 + 删源"）。自动更新和标签写入都走它。

顺带一提，这个函数不叫 `ReplaceFile`：Windows 的 `<windows.h>` 把那个名字定义成了宏，
主机测试里会被悄悄改写成 `ReplaceFileW`，一直到链接才报符号找不到。

### 4.4 系统时钟会影响 TLS

主机时间偏差过大时，所有证书都会被判为过期，curl 报错 60（对端证书验证失败）。
用户遇到过时钟被设成 2030 年、比实际快三年半的情况。

`curl_easy_strerror` 的文案毫无指向性，所以 `HttpClient::TlsFailureHint` 会拿系统年份
和构建年份（`__DATE__`）比，差距过大时在错误信息里直接说明是时间问题。

同理，错误信息里都带上了 curl 错误码：60（证书验证失败）和 77（CA 文件读不出来）
原因完全不同，只看文案分不出来。

### 4.5 netloader 与 ftpd 互斥

ftpd 一运行，hbmenu 的 netloader 就不再监听 28280，nxlink 用不了。
因此诊断结果写到 SD 卡上的文件而不是只依赖 nxlink 的 stdout。

另外：**不要用端口扫描去探测 28280**——一个空连接会让 hbmenu 退出等待状态。

### 4.6 `socketInitializeDefault` 不是引用计数的

第二次调用会直接失败。应用里有两处需要 socket（下载器的 curl、nxlink 的 stdout 重定向），
谁先谁后取决于启动方式，所以用 `net/SocketGuard` 包了一层引用计数。

### 4.7 Windows 上的 UTF-8 路径

`FileUtil` 里有 `#ifdef _WIN32` 分支用宽字符 API（`_wfopen` / `_wstat64` / `FindFirstFileW`）。
Windows 的窄字符 CRT 接口按 ANSI 代码页解释路径，UTF-8 的中文文件名会打不开——
这只影响主机端测试，Switch 上的 newlib 直接吃 UTF-8。

---

### 4.8 NRO 是整个读进内存才启动的

冷启动的黑屏时间里，有一段是 hbmenu 在读 NRO 本身。这条约束决定了一件事：
**不要往 romfs 里打包大文件**。一个二三十兆的中文字体会让每次启动都多等几秒，
所以自带字体走 SD 卡上的 `font.ttf` 而不是打包（见 3.2）。

启动路径上每个阶段的耗时都记在 `CApp::BootStage` 里，显示在「设置 → 关于」。
"启动要十秒"这种事光靠猜没用，得先有数。

## 5. 数据与配置

| 路径 | 内容 |
| --- | --- |
| `sdmc:/config/MusicPlayer2/config.ini` | 音量、播放模式、上次播放列表与位置、各项设置 |
| `sdmc:/config/MusicPlayer2/pathmap.ini` | Windows 路径 ↔ sdmc 路径的映射规则、默认音乐目录 |
| `sdmc:/config/MusicPlayer2/cacert.pem` | 可选。优先于 romfs 内置的那份 |
| `sdmc:/config/MusicPlayer2/diag.flag` | 存在则启用诊断；内容里每行一个目录路径会被枚举 |
| `sdmc:/config/MusicPlayer2/diag.log` | 诊断输出 |
| `sdmc:/config/MusicPlayer2/font.ttf` | 可选。用户自带字体，插在系统字体前面 |

数据目录在 0.6.3 之前是 `sdmc:/switch/MusicPlayer2/`。homebrew 的惯例是
配置放 `sdmc:/config/<应用名>/`、`/switch/` 只放 NRO，所以改了过来。
`CPlayer::MigrateLegacyDataDir` 会在启动时把旧位置的几个文件搬过去——
只搬我们自己写的那几个，新位置已有同名文件时不覆盖（那是用户更新过的版本）。

`PathMapper` 的前缀匹配必须是**最长匹配**：规则可能互相嵌套（`D:\Music\` 与
`D:\Music\ACG\`），按声明顺序匹配会让较短的规则抢先命中。这是主机端测试抓到的缺陷。
