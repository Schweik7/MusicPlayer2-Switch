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

### 6.0 发到哪里

移植版住在 [Schweik7/MusicPlayer2-Switch](https://github.com/Schweik7/MusicPlayer2-Switch)，
从上游的 fork（`Schweik7/MusicPlayer2`）里用 `git subtree split -P SwitchPort` 拆出来的，
历史原样保留。

每个版本发两处：

| 目标 | 为什么 |
| --- | --- |
| 新仓库的 Release | 主源，`Version.h` 里的 `MP2_SWITCH_REPO_NAME` 指向它 |
| 备用源 | GitHub 连不上时的回退，见 6.4 |

**过渡期已经结束。** 0.8.1 之前还要往旧仓库（上游的 fork `Schweik7/MusicPlayer2`）
发一份，因为装在机器上的 0.7.x 查的是那个地址；确认在用的机器都升到 0.8.x 之后，
旧仓库已删除。改仓库名天然有这个尾巴——**做替换的永远是旧版本的代码**，
所以新地址要等旧版本自己更新过一次才生效。以后再改地址仍然要按这个节奏走。

### 6.0.1 推送到独立仓库要用 cherry-pick，不能直接推 subtree split

独立仓库的 main 上有几个只存在于那边的提交（补 LICENSE 那次）。
`git subtree split` 每次都从 fork 的历史重算，算出来的 SHA 和远端对不上，
直接推会被判成非快进。正确做法：

```bash
git subtree split -P SwitchPort -b switch-standalone
git checkout -b release-tmp standalone/main
git cherry-pick <本次的几个提交在 split 分支上的 SHA>
git push standalone release-tmp:main
```

### 5.9 界面语言

`source/core/Lang.{h,cpp}`。走的是 gettext 那套：**用中文原文本身当键**，
不另起一套 `STR_XXX` 符号。

```cpp
m_rows[ITEM_THEME].label = T("配色");
```

这样选的理由：改动最小（264 处都是机械替换）、代码读起来还是中文、
缺翻译时自动退回中文——漏一条只是那一处显示中文，不会变成空白或 `STR_1234`。

**代价：改中文原文等于换了键，对应译文会失效退回中文。**
所以改文案时要顺手改 `Lang.cpp` 里的键。

三条容易踩的：

1. **相邻字面量拼接会炸。** 包 `T()` 之前 `"a" "b"` 由编译器拼成一条，
   包完就成了 `T("a") T("b")`，语法错误。跨行的、以及和宏拼的
   （`T("当前 ") MP2_SWITCH_VERSION`）都属于这一类，要改成运行时 `+` 拼接，
   或者合成一条完整的字面量去查表。
2. **`Diag::Logf` 的格式串不包 `T()`。** diag.log 是排错产物，
   用户发来的日志应该始终是同一种语言；而且格式串一旦不是编译期字面量，
   `-Wformat` 就查不了参数了。
3. **译文表有自检**，`Lang::ValidateTable()`，主机端测试会调。它保证两件事：
   printf 的格式说明符逐个对应（漏一个 `%s` 是崩溃，不是显示问题）、
   底栏提示按 `|` 的分段数一致（排版按段数均分间距）。

英文普遍比中文长，底栏和按钮上的词要刻意取短——那几处宽度固定，长了会被截断。

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

## 7. 自动更新是怎么实现的

这一节写得细，因为它踩过的坑最多，而且每个坑都只有实机能暴露。

### 7.1 整体流程

```
检查                    安装（本次运行）              下次启动
────────────────────   ─────────────────────────    ──────────────────────
GET  <源>/releases      GET  asset_url               ApplyPendingUpdate()
  ↓ ReleaseInfo::Parse    ↓ 写到 <self>.nro.new        ↓ rename → <self>.nro
tag / notes / url /       ↓ 三道校验                   ↓ 校验大小
size / asset_size         （不碰现有 NRO）             ↓ 删备份
```

分成两段是**必须**的，原因见 7.3。

### 7.2 两个源，一份解析代码

主源是 GitHub Release API，备用源是自建的
`https://download.psyventures.cn/mp2/latest.json`。

**备用源的 JSON 刻意做成和 GitHub Release API 一样的形状**，
于是 `ReleaseInfo::Parse` 一份代码解析两边——不必维护第二个解析器，
也不会出现"两边格式漂移"这种问题。发版时手写那份清单，字段见 6.4。

回退发生在两处，都只在"没拿到结果"时触发：

- **检查**失败 → 换源重查
- **下载**失败 → 换源重下。这一条是后补的：GitHub 的资产下载走另一个 CDN，
  接口查得到新版本、文件却下不全是常见情形

用户可以在设置里指定"仅备用源"完全跳过 GitHub——
GitHub 在部分地区要等到超时才失败，那十几秒是白等的。
`CUpdater::StartCheck(Source)` 带着这个选择，安装时沿用同一个源。

### 7.3 替换自身：必须赶在 romfsInit 之前

**这是整个功能里最关键的一条。**

挂着 romfs 的时候，正在运行的 NRO 文件被 FS 层按住：删不掉、改不了名、
也打不开写。romfs 就是从这个 NRO 文件里挂载的。

所以：

- **运行中途不替换自身**。下载完只把新版本留在 `<self>.nro.new`，不碰现有的 NRO
- `CApp::Init` 的**第一件事**是 `m_updater.ApplyPendingUpdate()`，排在 `romfsInit()` 之前。
  那时文件还没被任何东西按住，一个 `rename` 就换完了

排错时的表现：提示"更新已就绪，但写不进去"，SD 卡上
`.nro` / `.nro.bak` / `.nro.new` 三个文件同时存在——备份走了"复制"那条退路，
而"删源"那一步失败了。同时，程序**没运行**时用 FTP 写同一个文件完全正常，
这一条排除了"路径写错"之类的可能。

线索来自 [GBAStation](https://github.com/beiklive/GBAStation) 的更新器：
它在替换前先 `romfsExit()`。

> 顺序是本质的，挪回 `romfsInit()` 后面就又坏了。改动只有几行，
> 所以 `App.cpp` 里那段注释别删。

### 7.4 三道校验

改的是程序自己，写坏了下次就再也起不来，所以每一步都要能验：

| 时机 | 校验 | 失败后 |
| --- | --- | --- |
| 下载完 | 落到卡上的**实际字节数** == 收到的字节数 | 删掉临时文件，报数字 |
| 替换前 | 下载大小 == Release 声明的 `asset_size` | 删掉临时文件，**现有程序完好** |
| 替换后 | 装上去的大小 == 声明大小 | 还原备份 |

第一条不是多余的：**"交给 fwrite 多少字节"和"卡上真有多少字节"不是一回事**。
曾经 10.9MB 的更新在卡上只剩 2MB，而下载环节报的是成功——
根子是三处 `fclose` 的返回值被忽略了，缓冲区最后一次刷盘就发生在那里。

第二条用的是 `Status::asset_size` 而不是 `total`：`total` 会被进度回调改写、
开始下载时还会清零，拿它当基准校验会形同虚设。

只有校验通过才删备份。还原也失败时（最坏情况：程序位置上没有可用的 NRO），
状态里会写明备份在哪，让用户能手动改回来。

### 7.5 其它保守处理

- 下载地址不是 `https://` 开头时关闭证书校验（否则必然失败），
  同时把 `Status::asset_verified` 置为 false，界面上标出"无法验证服务器身份"。
  这是退路不是常态——镜像现在走 https，romfs 里的 CA 包含 ISRG Root X1/X2
- 装之前检查 NRO 魔数（文件偏移 `0x10` 处的 `NRO0`），挡住"下到一个 HTML 错误页
  然后把它当程序装上去"
- `MoveOverwrite` 先试 `rename`，失败退回"复制 + 删源"。
  它不叫 `ReplaceFile`：Windows 的 `<windows.h>` 把那个名字定义成了宏，
  主机测试里会被悄悄改写成 `ReplaceFileW`，一直到链接才报符号找不到

### 7.6 自己测这条链路

改过更新逻辑之后：

1. 发一个版本到 GitHub 和镜像（6.3、6.4）
2. **不要**用 FTP 把新 NRO 推到 SD 卡——那样就跳过了要测的东西
3. 在机器上用旧版本走一遍：检查 → 下载 → 退出 → 重新启动
4. 失败时先看 SD 卡上留下了哪些文件，那比错误信息更能说明卡在哪一步

注意有个引导问题：**做替换的是旧版本的代码**。
修的如果正是替换逻辑本身，那一跳仍然会失败，得先手动装上修好的版本，
从它开始的下一跳才是真正的验证。

---

## 8. 图标

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

## 9. Homebrew App Store 上架

上架方式是向 [fortheusers/switch-hbas-repo](https://github.com/fortheusers/switch-hbas-repo)
提 PR，在 `packages/` 下加一个目录，含：

| 文件 | 要求 |
| --- | --- |
| `pkgbuild.json` | 包定义，字段照抄现有包 |
| `icon.png` | **256 × 150** |
| `screen.png` | **848 × 208**，商店页顶部的横幅 |

尺寸不是文档里写的（那份文档还没写），是量现有包量出来的——
`packages/Donut`、`packages/FireplaceNX` 等都是这两个尺寸。

素材放在 `packaging/hbappstore/`，重新生成的脚本思路：

- **图标**：把 `icon.jpg` 缩到 132×132 居中放在 256×150 的主题色底上
- **横幅**：取一张实机截图的**歌词区**（那块只有文字，不含专辑封面，
  放进公开页面更稳妥），高斯模糊 + 压暗，叠一层从左到右的渐变把左半边压成近乎纯色，
  再放圆形图标和标题

`pkgbuild.json` 里的 `assets[].url` 指向 Release 的 NRO，`dest` 是 `/switch/MusicPlayer2.nro`。
每次发版后要更新其中的 `version` 和下载地址。

---

## 10. 代码约定

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
