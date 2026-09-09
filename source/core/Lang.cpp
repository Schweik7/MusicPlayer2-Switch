#include "Lang.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace
{
    Lang::Language g_lang = Lang::LANG_ZH;

    struct Pair
    {
        const char* zh;
        const char* en;
    };

    // 英文译文表。键是中文原文。
    //
    // 两条硬规则：
    //   1. printf 的格式说明符必须原样保留，个数和顺序都不能变
    //   2. 底栏提示用 '|' 分段，段数要和中文一致——排版按段数均分间距
    //
    // 英文普遍比中文长，底栏和按钮上的词刻意取短：
    // 那几处宽度是固定的，长了会被截断，宁可用缩写。
    const Pair kEnglish[] = {
        // —— 拼接用的片段 ——
        { " / 应为 ", " / expected " },
        { " 分钟", " min" },
        { " 即可", "" },
        { " 字节），已放弃更新", " bytes), update abandoned" },
        { " 改名为 ", " renamed to " },
        { " 秒", " s" },
        { "、封面", ", cover" },
        { "（CA 证书包读不出来）", " (CA bundle unreadable)" },
        { "（备用源）", " (mirror)" },
        { "（点这里或按 X 输入）", " (tap here or press X)" },
        { "（系统时间为 ", " (system clock reads " },
        { "）。退出后把 ", "). After exiting, rename " },
        { "，但该版本没有附带 ", ", but that release has no " },
        { "，证书可能因此被判为过期，请先校准主机时间）", ", so the certificate may look expired; set the console clock first)" },
        { "，请手动改回 ", ", please rename it back to " },
        { "，重启应用即可完成更新", "; restart the app to finish updating" },
        { "；备用源不可用：", "; mirror unavailable: " },
        { "；备用源也失败：", "; mirror failed too: " },

        // —— 路径映射文件的表头 ——
        { "# MusicPlayer2 for Switch - 路径映射\n", "# MusicPlayer2 for Switch - path mapping\n" },
        { "# 例：D:\\Music\\ = sdmc:/music/\n\n", "# Example: D:\\Music\\ = sdmc:/music/\n\n" },
        { "# 左边是桌面版播放列表里的 Windows 路径前缀，右边是 SD 卡上的对应目录。\n",
          "# Left: the Windows path prefix used in desktop playlists. Right: the matching folder on the SD card.\n" },

        // —— 底栏键位提示。段数必须和中文一致 ——
        { "A 下载|X 搜索|Y 音乐源|ZL/ZR 歌词/封面|＋ 写入文件|B 返回",
          "A Download|X Search|Y Source|ZL/ZR Lyrics/Cover|＋ Embed|B Back" },
        { "A 修改/执行|B 返回|方向键 移动|左右 切换栏",
          "A Change|B Back|D-Pad Move|L/R Column" },
        { "A 打开/播放|B 上级目录|X 播放整个目录|Y 设为音乐目录|− 播放列表|＋ 设置",
          "A Open|B Up|X Play folder|Y Set as library|− Playlist|＋ Settings" },
        { "A 播放选中项|B 返回|L/R 翻页|＋ 设置",
          "A Play|B Back|L/R Page|＋ Settings" },
        { "ZL/ZR ±5秒|右摇杆↑↓ 翻歌词|L 封面大小|R 单栏/双栏|LS 触摸|RS 下载|B×2 退出|− 列表|＋ 设置",
          "ZL/ZR ±5s|R-Stick ↑↓ Lyrics|L Cover|R 1/2 col|LS Touch|RS Download|B×2 Exit|− Playlist|＋ Settings" },
        { "B 返回设置", "B Back to settings" },
        { "X 播放整个目录", "X Play whole folder" },
        { "Y 设为音乐目录", "Y Set as music folder" },

        // —— 启动与致命错误 ——
        { "App::Init 失败: %s", "App::Init failed: %s" },
        { "\n MusicPlayer2 启动失败\n\n %s\n\n 按 + 退出。\n",
          "\n MusicPlayer2 failed to start\n\n %s\n\n Press + to exit.\n" },
        { "SDL_CreateRenderer 失败: ", "SDL_CreateRenderer failed: " },
        { "SDL_CreateWindow 失败: ", "SDL_CreateWindow failed: " },
        { "SDL_Init 失败: ", "SDL_Init failed: " },
        { "SDL_InitSubSystem(AUDIO) 失败: ", "SDL_InitSubSystem(AUDIO) failed: " },
        { "SDL_NewAudioStream 失败: ", "SDL_NewAudioStream failed: " },
        { "TTF_Init 失败: ", "TTF_Init failed: " },
        { "Mix_OpenAudio 失败: ", "Mix_OpenAudio failed: " },
        { "Mix_PlayMusic 失败: ", "Mix_PlayMusic failed: " },
        { "curl_easy_init 失败", "curl_easy_init failed" },
        { "plInitialize 失败，无法取得系统共享字体",
          "plInitialize failed; cannot load the shared system font" },
        { "没有可用的系统字体", "No usable system font" },

        // —— 音频 ——
        { "FLAC 定位失败", "FLAC seek failed" },
        { "FLAC 数据中存在损坏的帧，已跳过", "Corrupt FLAC frame skipped" },
        { "FLAC 文件缺少有效的 STREAMINFO", "FLAC file has no valid STREAMINFO" },
        { "FLAC__stream_decoder_new 失败", "FLAC__stream_decoder_new failed" },
        { "读取 FLAC 元数据失败", "Failed to read FLAC metadata" },
        { "无法打开 FLAC 文件: ", "Cannot open FLAC file: " },
        { "音频引擎尚未初始化", "Audio engine is not initialised" },
        { "无法播放该文件", "Cannot play this file" },
        { "无法解码: ", "Cannot decode: " },
        { "该格式不支持定位: ", "Seeking is not supported for this format: " },

        // —— 播放界面 ——
        { "正在播放", "Now playing" },
        { "播放", "Play" },
        { "暂停", "Pause" },
        { "已停止", "Stopped" },
        { "视图", "View" },
        { "模式", "Mode" },
        { "歌词", "Lyrics" },
        { "封面", "Cover" },
        { "当前曲目封面", "Current cover" },
        { "暂无歌词", "No lyrics" },
        { "没有正在播放的曲目", "Nothing is playing" },
        { "音量 %d%%", "Volume %d%%" },
        { "±1 秒", "±1 s" },
        { "歌词 %+.1f 秒", "Lyrics %+.1f s" },
        { "歌词延后 0.5 秒", "Lyrics delayed by 0.5 s" },
        { "歌词提前 0.5 秒", "Lyrics advanced by 0.5 s" },
        { "歌词改为单栏", "Lyrics: single column" },
        { "歌词改为双栏（左原文 右译文）",
          "Lyrics: two columns (original left, translation right)" },
        { "把同名 .lrc 文件放在歌曲旁边，或按下右摇杆在线下载",
          "Put a matching .lrc next to the track, or press the right stick to download" },
        { "按下右摇杆可以在线下载", "Press the right stick to download" },
        { "拖动歌词只是翻看，松手后自动归位",
          "Dragging only browses the lyrics; it snaps back when you let go" },
        { "拖动歌词将同时改变播放进度", "Dragging the lyrics also seeks playback" },
        { "再按一次 B 退出", "Press B again to exit" },
        { "再点一次 B 退出", "Tap B again to exit" },
        { "点一下或按 B 返回", "Tap or press B to go back" },
        { "把 background.jpg 放进 ", "Put background.jpg in " },

        // —— 播放模式 ——
        { "顺序播放", "In order" },
        { "单曲播放", "Single track" },
        { "单曲循环", "Repeat one" },
        { "列表循环", "Repeat all" },
        { "随机播放", "Shuffle" },

        // —— 播放列表 / 浏览 ——
        { "列表", "Playlist" },
        { "播放列表", "Playlist" },
        { "在线下载", "Download" },
        { "播放列表为空", "The playlist is empty" },
        { "播放列表已有内容，丢弃后台扫描结果",
          "Playlist already has tracks; discarding the background scan" },
        { "无法读取播放列表: ", "Cannot read playlist: " },
        { "无法读取该播放列表", "Cannot read that playlist" },
        { "按 + 从 SD 卡添加音乐", "Press + to add music from the SD card" },
        { "按 ＋ 浏览 SD 卡上的音乐", "Press ＋ to browse the SD card" },
        { "浏览 SD 卡", "Browse SD card" },
        { "选择要播放的目录", "Pick a folder to play" },
        { "无法打开目录: ", "Cannot open folder: " },
        { "该目录下没有可播放的音频", "No playable audio in this folder" },
        { "该目录下没有音频文件或子目录", "No audio files or subfolders here" },
        { "已设为默认音乐目录（目录内没有可播放的音频）",
          "Set as the music folder (it contains no playable audio)" },
        { "已设为默认音乐目录，载入 %d 首", "Set as the music folder, %d tracks loaded" },
        { "音乐库扫描完成，共 %d 首", "Library scan finished: %d tracks" },
        { "后台扫描完成：%u 首，耗时 %u 毫秒",
          "Background scan finished: %u tracks in %u ms" },
        { "正在扫描音乐库…", "Scanning the music library…" },
        { "恢复上次会话", "Restoring the last session" },
        { "未知专辑", "Unknown album" },
        { "未知曲目", "Unknown track" },
        { "未知艺术家", "Unknown artist" },

        // —— 设置 ——
        { "设置", "Settings" },
        { "配色", "Theme" },
        { "深色", "Dark" },
        { "浅色", "Light" },
        { "语言", "Language" },
        { "空闲自动变暗", "Dim when idle" },
        { "自动变暗：", "Dim when idle: " },
        { "显示歌词翻译", "Show lyric translation" },
        { "拖歌词跟随进度", "Dragging lyrics seeks" },
        { "关闭（仅翻看）", "Off (browse only)" },
        { "歌词时间偏移", "Lyric time offset" },
        { "歌词时间偏移已关闭并归零", "Lyric time offset turned off and reset to zero" },
        { "歌词时间偏移：播放界面按住 B + 方向键左右调整",
          "Lyric offset: hold B and press left/right on the player screen" },
        { "开启 · 按住 B + 方向键（%+.1f 秒）", "On · hold B + D-Pad (%+.1f s)" },
        { "歌词区背景", "Lyric background" },
        { "歌词区背景：", "Lyric background: " },
        { "歌词区背景：自定义图片", "Lyric background: custom image" },
        { "自定义图片", "Custom image" },
        { "隐藏底部键位提示", "Hide the button hints" },
        { "底部键位提示已显示", "Button hints shown" },
        { "底部键位提示已隐藏，歌词区随之变高",
          "Button hints hidden; the lyric area grows to match" },
        { "下载歌词封面后嵌入歌曲文件", "Embed downloads into the audio file" },
        // 英文比中文宽得多，这一项的标签又长，值必须短到只剩一个词，
        // 否则会和标签叠上。中文那边照常显示完整说明。
        { "关闭（存同级目录）", "Off" },
        { "下载的歌词封面存到歌曲的同级目录",
          "Downloaded lyrics and covers are saved next to the track" },
        { "下载的歌词封面将写进 MP3 / FLAC 文件",
          "Downloaded lyrics and covers will be written into MP3 / FLAC files" },
        { "默认音乐目录", "Music folder" },
        { "网络", "Network" },
        { "已连接 · 证书已验证", "Connected · certificate verified" },
        { "已连接 · 证书未验证", "Connected · certificate not verified" },
        { "不可用", "Unavailable" },
        { "关于", "About" },
        { "版本", "Version" },
        { "构建时间", "Built" },
        { "程序位置", "Location" },
        { "启动耗时", "Startup" },
        { "启动合计 %u 毫秒", "%u ms total" },
        { "启动阶段 %-12s %u 毫秒", "Stage %-12s %u ms" },
        { "子系统", "Subsystem" },
        { "图形与字体", "Graphics and fonts" },
        { "图形与字体(自带)", "Graphics and fonts (bundled)" },
        { "音频与网络", "Audio and network" },
        { "项目地址", "Project" },
        { "上游项目", "Upstream" },
        { "本移植版重写了界面与音频层：桌面版基于 MFC，无法交叉编译到 Switch。",
          "This port rewrites the UI and audio layers: the desktop version is MFC-based and cannot be cross-compiled for the Switch." },
        { "核心的播放列表、歌词解析、在线下载逻辑与桌面版保持一致。",
          "The playlist, lyric parsing and online download logic match the desktop version." },
        { "触摸 开", "Touch on" },
        { "触摸 关", "Touch off" },
        { "已启用触摸操作", "Touch controls enabled" },
        { "沉浸模式：触摸已关闭，按下左摇杆（LS）恢复",
          "Immersive mode: touch is off, press the left stick (LS) to restore" },
        { "已切换到浅色配色", "Switched to the light theme" },
        { "已切换到深色配色", "Switched to the dark theme" },
        { "已切换到 ", "Switched to " },
        { "省电模式 · 按任意键唤醒", "Power saving · press any button to wake" },

        // —— 更新 ——
        { "检查更新", "Check for updates" },
        { "检查更新失败：", "Update check failed: " },
        { "更新源", "Update source" },
        { "更新源：", "Update source: " },
        { "自动（GitHub 优先）", "Automatic (GitHub first)" },
        { "仅 GitHub", "GitHub only" },
        { "仅备用源（更快）", "Mirror only (faster)" },
        { "正在检查更新…", "Checking for updates…" },
        { "正在下载新版本…", "Downloading the new version…" },
        { "发现新版本 ", "New version available: " },
        { "已是最新版本（", "Already up to date (" },
        { "更新正在进行中", "An update is already running" },
        { "主源下载失败，改用备用源…", "Primary source failed, falling back to the mirror…" },
        { "按 A 下载 · 下载后需重启应用才能生效",
          "Press A to download · restart the app afterwards to apply" },
        { "已应用新版本，重新启动后生效",
          "The new version has been applied; it takes effect after a restart" },
        { "更新已就绪，但写不进去（errno=",
          "The update is ready but could not be written (errno=" },
        { "更新已就绪，但换不动正在使用的文件（errno=",
          "The update is ready but the running file could not be replaced (errno=" },
        { "更新文件不完整，已丢弃", "The downloaded update was incomplete and was discarded" },
        { "更新写入后校验不通过，已还原原有版本",
          "The written update failed verification; the previous version was restored" },
        { "下载到的文件不是有效的 NRO，已丢弃",
          "The downloaded file is not a valid NRO and was discarded" },
        { "替换原文件失败，原文件仍然完好",
          "Could not replace the original file; it is still intact" },
        { "还原失败，原版本仍在 ", "Restore failed; the previous version is still at " },
        { "若重启后仍未生效，把 ", "If a restart does not apply it, rename " },
        { "来自备用源，无法验证服务器身份",
          "From the mirror; the server identity cannot be verified" },
        { "未验证证书", "Certificate not verified" },
        { "缺少 CA 证书包，无法验证服务器身份",
          "No CA bundle, so the server identity cannot be verified" },

        // —— 下载界面 ——
        { "正在下载", "Downloading" },
        { "正在下载：", "Downloading: " },
        { "已下载 ", "Downloaded " },
        { "下载成功", "Download complete" },
        { "下载失败", "Download failed" },
        { "下载失败：", "Download failed: " },
        { "无法开始下载", "Cannot start the download" },
        { "下载不完整（收到 %llu / %llu 字节）",
          "Incomplete download (%llu of %llu bytes)" },
        { "下载的文件大小不对（", "The downloaded file has the wrong size (" },
        { "写入 SD 卡不完整（卡上 %llu / 应有 %llu 字节）",
          "Incomplete write to the SD card (%llu of %llu bytes on the card)" },
        { "写入 SD 卡失败（卡满或写入出错）",
          "Writing to the SD card failed (full or write error)" },
        { "无法写入 ", "Cannot write " },
        { "文件不存在: ", "File does not exist: " },
        { "服务器返回 HTTP %ld", "Server returned HTTP %ld" },
        { "响应内容过大", "Response too large" },
        { "网络不可用", "Network unavailable" },
        { "网络未初始化", "Network not initialised" },
        { "网络未连接", "Not connected" },
        { "正在请求……", "Requesting…" },
        { "取消", "Cancel" },
        { "已取消", "Cancelled" },
        { "已请求取消", "Cancellation requested" },
        { "退出", "Exit" },
        { "搜索歌曲（歌手 + 歌名）", "Search for a track (artist + title)" },
        { "搜索：", "Search: " },
        { "正在搜索：", "Searching: " },
        { "搜索失败：", "Search failed: " },
        { "请先输入搜索关键词", "Enter something to search for first" },
        { "按 A 或 X 开始搜索", "Press A or X to search" },
        { "找到 %d 个结果", "%d results" },
        { "没有搜索到匹配的歌曲", "No matching tracks found" },
        { "没有找到足够匹配的结果，请手动选择",
          "No close enough match; please pick one manually" },
        { "网易云音乐", "NetEase Cloud Music" },
        { "QQ音乐", "QQ Music" },
        { "下载歌词：开", "Lyrics: on" },
        { "下载歌词：关", "Lyrics: off" },
        { "下载封面：开", "Cover: on" },
        { "下载封面：关", "Cover: off" },
        { "歌词和封面至少要选一项", "Pick at least one of lyrics or cover" },
        { "写入文件", "Embed" },
        { "写入歌曲文件：开（仅 MP3 / FLAC）", "Embed into the track: on (MP3 / FLAC only)" },
        { "写入歌曲文件：关，只存在歌曲旁边",
          "Embed into the track: off, saved alongside instead" },
        { "已写入文件", "Written into the file" },
        { "已写入歌曲文件", "Written into the track" },
        { "嵌入失败：", "Embedding failed: " },
        { "这种格式不支持嵌入", "Embedding is not supported for this format" },
        { "标签结构不认识，未改动原文件",
          "Unrecognised tag layout; the file was left untouched" },
        { "读不了原文件", "Cannot read the original file" },
        { "写临时文件失败，未改动原文件",
          "Could not write the temporary file; the original was left untouched" },
        { "歌词下载失败：", "Lyric download failed: " },
        { "歌词写入失败：", "Could not save the lyrics: " },
        { "封面下载失败：", "Cover download failed: " },
        { "封面写入失败：", "Could not save the cover: " },
        { "封面信息获取失败：", "Could not fetch cover details: " },
        { "封面数据无效", "Invalid cover data" },
        { "封面文件解码失败 %s: %s", "Failed to decode cover file %s: %s" },
        { "封面纹理创建失败 %s: %s", "Failed to create cover texture %s: %s" },
        { "内嵌封面解码失败 (%s, %u 字节): %s",
          "Failed to decode embedded cover (%s, %u bytes): %s" },
        { "没有找到可用封面: %s", "No usable cover found: %s" },
        { "这首歌没有可用的封面", "This track has no usable cover" },
        { "这首歌没有封面", "This track has no cover" },
        { "这首歌没有歌词", "This track has no lyrics" },
        { "发现 ", "Found " },
        { "当前 ", "Current: " },
        { "合计", "Total" },

        // —— 通用 ——
        { "开启", "On" },
        { "关闭", "Off" },
        { "显示", "Shown" },
        { "隐藏", "Hidden" },
        { "是", "Yes" },
        { "否", "No" },
        { "未知", "Unknown" },
        { "未知错误", "Unknown error" },
        { "正在启动 · ", "Starting · " },
        { "默认音乐目录: %s (是目录=%s)", "Music folder: %s (is a folder=%s)" },

        // —— 星期。日期格式是 "09/08 周二"，英文用三字母缩写 ——
        { "周日", "Sun" },
        { "周一", "Mon" },
        { "周二", "Tue" },
        { "周三", "Wed" },
        { "周四", "Thu" },
        { "周五", "Fri" },
        { "周六", "Sat" },
    };

    // 第一次用到时建表。表是只读的，建好之后不再变，
    // 所以返回表里 std::string 的 c_str() 是稳定的。
    const std::unordered_map<std::string, std::string>& EnglishTable()
    {
        static const std::unordered_map<std::string, std::string> table = [] {
            std::unordered_map<std::string, std::string> m;
            m.reserve(sizeof(kEnglish) / sizeof(kEnglish[0]) * 2);
            for (const Pair& p : kEnglish)
                m.emplace(p.zh, p.en);
            return m;
        }();
        return table;
    }
}

namespace Lang
{

void SetLanguage(Language lang)
{
    if (lang < 0 || lang >= LANG_COUNT)
        lang = LANG_ZH;
    g_lang = lang;
}

Language GetLanguage()
{
    return g_lang;
}

const char* GetLanguageName(Language lang)
{
    // 各自用自己的语言写：切换时用户看到的是目标语言的名字，
    // 不认识当前语言也能找回去
    switch (lang)
    {
    case LANG_EN: return "English";
    case LANG_ZH:
    default:      return "简体中文";
    }
}

namespace
{
    // 按 printf 的规则把格式说明符抽出来，"%%" 不算
    std::string FormatSpecs(const char* s)
    {
        std::string out;
        for (const char* p = s; *p != '\0'; ++p)
        {
            if (*p != '%')
                continue;
            if (*(p + 1) == '%')
            {
                ++p;
                continue;
            }
            const char* start = p++;
            while (*p != '\0' && std::strchr("diouxXeEfgGaAcspn%", *p) == nullptr)
                ++p;
            if (*p == '\0')
                break;
            out.append(start, static_cast<size_t>(p - start) + 1);
            out.push_back(' ');
        }
        return out;
    }

    int CountSegments(const char* s)
    {
        int n = 0;
        for (const char* p = s; *p != '\0'; ++p)
        {
            if (*p == '|')
                ++n;
        }
        return n;
    }
}

const char* ValidateTable()
{
    for (const Pair& p : kEnglish)
    {
        // 译文留空是"英文下这里就该什么都不显示"的写法，跳过检查
        if (p.en[0] == '\0')
            continue;
        if (FormatSpecs(p.zh) != FormatSpecs(p.en))
            return p.zh;
        if (CountSegments(p.zh) != CountSegments(p.en))
            return p.zh;
    }
    return nullptr;
}

const char* Translate(const char* zh)
{
    if (zh == nullptr || g_lang == LANG_ZH)
        return zh;

    const std::unordered_map<std::string, std::string>& table = EnglishTable();
    auto it = table.find(zh);
    if (it == table.end())
        return zh;              // 缺翻译就显示中文，总比空白强

    // 空串是"这里英文下就该什么都不显示"的写法（比如中文里用来补语气的片段）
    return it->second.c_str();
}

}   // namespace Lang
