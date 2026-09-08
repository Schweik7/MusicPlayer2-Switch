# 开发指南

构建、测试、部署、发版的完整流程。架构说明见 [ARCHITECTURE.md](ARCHITECTURE.md)。

---

## 1. 环境

需要 [devkitPro](https://devkitpro.org/wiki/Getting_Started) 的 Switch 开发环境：

```
pacman -S switch-dev switch-sdl2 switch-sdl2_mixer switch-sdl2_ttf switch-sdl2_image \
          switch-curl switch-mbedtls switch-flac switch-libjpeg-turbo switch-libpng
```

主机端测试还需要 Visual Studio 或 Build Tools（脚本用 `vswhere` 自动定位）。

### Windows 上不需要 WSL2

devkitPro 的 Windows 安装包自带 MSYS2。唯一的约束是 `make` 必须跑在那个 MSYS2 环境里
（Makefile 依赖 `$DEVKITPRO`、devkitPro 版的 `make` 和一系列 POSIX 工具），
`build.ps1` 就是干这件事的：定位 devkitPro、把 `D:\a\b` 转成 `/d/a/b`、
写一份 LF 换行的 shell 脚本再交给 MSYS2 的 bash。

---

## 2. 构建

```powershell
pwsh SwitchPort/build.ps1            # 增量
pwsh SwitchPort/build.ps1 clean      # 清理
pwsh SwitchPort/build.ps1 rebuild    # 全量重建
```

Linux / macOS 直接 `cd SwitchPort && make`。

产物是 `SwitchPort/MusicPlayer2.nro`（约 10 MB）。

### 构建配置里几个不显然的地方

- **`-isystem` 而不是 `-I`** 引入第三方头文件。libnx 的头在 `-Wextra` 下会刷出几千条
  `missing-field-initializers`，用 `-isystem` 可以只对我们自己的代码保留 `-Wextra` 的价值。
- **链接顺序取自 `pkg-config --static --libs`**，不要手写。曾经因为猜 `-lvorbisfile`、
  漏 `-lharfbuzz` 而链接失败。
- **模板默认关掉了异常和 RTTI**，本项目的 CXXFLAGS 把它们打开了。
- `-D__SWITCH__` 用于区分平台（`FileUtil::CommitDevice` 等）。

**要求：全量重建零警告。** 提交前跑一次 `rebuild` 确认。

---

## 3. 主机端测试

```powershell
pwsh SwitchPort/tests/build_host_test.ps1
```

只编译平台无关的源文件（见 ARCHITECTURE 第 2 节），用 MSVC 直接编译运行，
**不需要真机也不需要联网**。目前 688 条断言。

### 覆盖范围

字符串处理、路径映射、播放列表读写、LRC 解析、标签解析（含内嵌封面）、
**标签写入**（ID3v2 的 APIC/USLT、FLAC 的 PICTURE/VORBIS_COMMENT）、
**时长估算**（MP3 的 Xing/Info/VBRI、WAV、Ogg/Opus 的 granule、FLAC 的 STREAMINFO）、
媒体扫描、配置、音频环形缓冲（含双线程）、JSON、URL 编码、歌曲匹配、
网易云/QQ 歌词源、下载流程编排、版本比较、GitHub Release 响应解析。

### 加新测试

1. 测试文件放 `tests/`，导出一个 `RunXxxTests()`
2. 在 `tests/host_test.cpp` 里声明并调用
3. 在 `tests/build_host_test.ps1` 的源文件列表里加上它和被测的 `.cpp`

断言宏：`CHECK` / `CHECK_EQ`（字符串）/ `CHECK_EQ_INT` / `CHECK_NEAR`。

### 两条经验

**测试必须是密闭的。** 用到磁盘的测试要在开头调 `TestFramework::RemoveTestDir()`。
曾经因为上一轮遗留的 `y.flac` 让"扫描到几个音频"的断言依赖历史状态而误报。

**合成夹具替代不了真实数据。** 这一条已经应验两次：

- ID3v2 的 UTF-16 文本被截掉最后一个字符：65 条合成断言一条都没抓到——
  夹具全用中文，汉字在 UTF-16 里高字节非零，恰好绕开了出问题的那条路径
- MP3 时长算不出来：合成的 ID3v2 夹具只有几 KB，而真实文件的内嵌封面能把标签
  撑到 640KB，第一个音频帧根本不在头部缓冲里

两次都是拿用户的真实音乐文件跑一遍才暴露的，之后才补上对应的回归用例。

**改文件的功能，验证要到字节级。** 标签写入会重写用户的音乐文件，
所以除了单元测试，还用真实的 MP3/FLAC 端到端跑过：嵌入前后把音频段抽出来逐字节比对，
确认解析出的时长没变、重复嵌入文件大小不增长（旧的那份被替换而不是叠加）。

写解析器时建议两边都过：合成夹具覆盖边界和损坏输入，真实文件验证"确实能用"。
一次性的真实文件校验工具可以照 `docs/` 外的 scratchpad 做法，编译一个小 main
链接 `core/AudioTag.cpp` + `core/FileUtil.cpp` + `core/StringUtil.cpp` 即可。

---

## 4. 部署到实机

### 方式一：FTP（推荐）

在 Switch 上跑 ftpd，然后：

```bash
curl -T SwitchPort/MusicPlayer2.nro "ftp://<IP>:<PORT>/switch/MusicPlayer2.nro"
```

10MB 大约 8 秒。好处是不打断正在运行的东西，也能顺手取回诊断日志。

### 方式二：nxlink

```bash
nxlink -a <IP> -s SwitchPort/MusicPlayer2.nro
```

推送并直接运行，`-s` 会把 stdout 接回开发机。

**两个坑**：
- ftpd 一运行，hbmenu 的 netloader 就不再监听，nxlink 用不了，两者互斥
- **不要用端口扫描探测 28280**，一个空连接会让 hbmenu 退出等待状态

---

## 5. 实机诊断

有些问题只有真机能暴露（文件系统行为、字体覆盖、TLS）。程序内置了诊断模式。

### 启用

在 `sdmc:/config/MusicPlayer2/` 放一个 `diag.flag`（旧位置 `sdmc:/switch/MusicPlayer2/`
仍然认）。通过 nxlink 启动时自动启用。

文件内容里**每行一个目录路径**（`sdmc:/` 开头）会被逐条枚举并 `stat`，例如：

```
sdmc:/
sdmc:/switch/dao_chu
```

### 输出

写到 `sdmc:/config/MusicPlayer2/diag.log`（同时打到 stdout）。内容包括：

- 文件系统对不同 UTF-8 编码长度的文件名的支持情况（创建 / stat / 重新打开 / readdir 回读）
- 指定目录的逐条枚举结果与 stat 成败统计
- 系统共享字体对中日韩字形的覆盖情况
- 网络与 TLS 自检：系统时间、CA 证书包位置与证书条目数，并真的发一次
  更新检查用的请求，把 curl 错误码原样记下来
- 音乐库扫描耗时、读到标签的数量、前几首的文件名与标题对照

日志按 20 行提交一次到 SD 卡，程序崩溃时最后几行不会丢。

### 用完记得删掉 diag.flag

**诊断模式会让启动慢好几秒**，而且全在首帧之前同步做完：

- 往 SD 卡写若干探针文件，每个都要 `fsdevCommitDevice`
- 把 `diag.flag` 里列的每个目录逐条枚举并 `stat`（用户那台机器上是 1259 次）
- 字体覆盖探测会强制把六档字号的回退链全建起来，正好抵消掉按需加载
- 网络自检真的发一次 HTTPS 请求到 GitHub API

这是诊断该付的代价，但很容易忘了它还开着，然后把慢启动当成程序本身的问题——
排查"启动要十秒"时就绕了这么一圈。先看「设置 → 关于」里的分阶段耗时，
再确认 `diag.flag` 在不在。

取回：

```bash
curl "ftp://<IP>:<PORT>/config/MusicPlayer2/diag.log" -o diag.log
```

---

## 6. 发版

### 6.1 改版本号（两处，必须一起改）

| 位置 | 用途 |
| --- | --- |
| `source/Version.h` 的 `MP2_SWITCH_VERSION` | 更新检查拿它和 Release 的 `tag_name` 比 |
| `Makefile` 的 `APP_VERSION` | hbmenu 里显示的版本 |

版本号与 Release tag 必须对应（tag 写 `v0.5.1`，`MP2_SWITCH_VERSION` 写 `0.5.1`，
`VersionUtil::Parse` 会忽略前导的 `v`）。

**不要跳版本号。** 修 bug 走补丁位。

### 6.2 构建与验证

```powershell
pwsh SwitchPort/build.ps1 rebuild         # 零警告
pwsh SwitchPort/tests/build_host_test.ps1 # 全绿
```

### 6.3 发布

```bash
gh api -X POST repos/<owner>/<repo>/releases \
  -f tag_name=v0.5.1 -f target_commitish=feature/switch-port \
  -f name='Switch 移植版 v0.5.1' -F draft=false -F prerelease=false --jq '.id'

gh release upload v0.5.1 SwitchPort/MusicPlayer2.nro --clobber -R <owner>/<repo>

gh api -X PATCH repos/<owner>/<repo>/releases/<id> --field body=@notes.md
```

**`gh release create` 会报一句误导性的 "workflow scope may be required"**，
即使 token 已有该权限。用上面的 API 调用绕开。

发完核对资产大小与本地构建一致：

```bash
gh api repos/<owner>/<repo>/releases/latest --jq '.assets[0].size'
stat -c%s SwitchPort/MusicPlayer2.nro
```

### 6.4 同步到备用源

GitHub 在部分地区访问不稳定，所以更新检查失败时会回退到自建镜像
`https://download.psyventures.cn/mp2/`。发完 GitHub Release 之后同步过去：

```bash
scp SwitchPort/MusicPlayer2.nro root@<主机>:/var/www/download/mp2/
ssh root@<主机> "cat > /var/www/download/mp2/latest.json" <<'JSON'
{
  "tag_name": "v0.6.4",
  "body": "……更新说明……",
  "assets": [{
    "name": "MusicPlayer2.nro",
    "size": 10899777,
    "browser_download_url": "https://download.psyventures.cn/mp2/MusicPlayer2.nro"
  }]
}
JSON
```

**清单刻意做成和 GitHub Release API 一样的形状**，这样 `ReleaseInfo::Parse`
一份代码解析两边，不必维护第二个解析器，也不会出现"两边格式漂移"这种问题。

镜像地址写在 `source/Version.h` 的 `MP2_SWITCH_MIRROR_URL`。

#### 服务器上的两条路

同一份文件同时通过两个入口提供，nginx 配置在 `/etc/nginx/sites-available/mp2-mirror`：

| 入口 | 用途 |
| --- | --- |
| `https://download.psyventures.cn/...` | 正式地址，Let's Encrypt 证书，certbot 自动续期 |
| `http://39.99.245.245:8888/...` | 明文端口，域名解析不可用时兜底 |

8888 这个端口号不是随便挑的：阿里云安全组只放行了若干区间，
6742 之类的端口在外网直接超时。判断方法是从外网连一下——
被安全组挡住是超时，端口开放但没服务是立刻拒绝。

8888 上还留了两条 `location =`，把根路径的 `/latest.json` 和 `/MusicPlayer2.nro`
指到 `mp2/` 子目录下。0.6.3 那一版把这两个地址写死在根上了，
文件后来归到子目录，这两条保证那批已发出去的版本还能更新。

#### 为什么镜像一定要走 https

明文 HTTP 下发一个会被执行的 NRO，等于把"装什么程序"交给网络路径上的任何人。
`CUpdater` 对此的处理是：下载地址不是 `https://` 开头时关闭证书校验
（否则必然失败），同时把 `Status::asset_verified` 置为 false，
设置界面据此显示"来自备用源，无法验证服务器身份"。

这是退路不是常态。romfs 里的 CA 包含 ISRG Root X1/X2，
所以 Let's Encrypt 签的证书能直接验通。

### 6.5 更新功能依赖的约定

`net/Updater` 在 Release 的 `assets` 里按**文件名**查找，见 `Version.h` 的
`MP2_SWITCH_ASSET_NAME`（`MusicPlayer2.nro`）。改名会让旧版本认为"有新版本但没带 NRO"。

---

## 7. 图标

`SwitchPort/icon.jpg`，256×256 JPEG。缺失时 Makefile 会回落到 devkitPro 的默认图标。

从项目自带的 ico 重新生成：

```python
from PIL import Image
im = Image.open("MusicPlayer2/res/MusicPlayer2.ico")
icon = im.convert('RGBA').resize((256, 256), Image.LANCZOS)
bg = Image.new('RGB', (256, 256), (0x1F, 0x1F, 0x27))   # JPEG 不支持透明
bg.paste(icon, (0, 0), icon)
bg.save("SwitchPort/icon.jpg", "JPEG", quality=92, subsampling=0)
```

---

## 8. 代码约定

- **UTF-8 贯穿始终**。只有 Windows 分支和 UTF-16 标签解码处才出现宽字符。
- **注释写"为什么"，不写"是什么"**。尤其是绕开平台缺陷的地方，
  要写清楚缺陷是什么、为什么这么绕。
- **新的解析类逻辑放进 `core/` 或 `net/` 的平台无关部分**，否则测不了。
- 平台特有的行为（`fsdevCommitDevice`、`rename` 不可靠）优先封装进 `FileUtil`，
  不要散落到各处。

### 一个工具链上的坑

用 heredoc（`<<'EOF'`）往脚本里写含反斜杠转义的 C++ 代码时，`\0`、`\n` 之类会被吃掉，
生成源码里出现真的空字节或换行，编译报"空字符常量"或"常量中有换行符"。
写这类内容用 Edit/Write 工具，或在脚本里用 `chr(92)` 拼出反斜杠。
