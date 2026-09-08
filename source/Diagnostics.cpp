#include "Diagnostics.h"
#include "SystemClock.h"
#include "core/FileUtil.h"
#include "net/HttpClient.h"
#include "net/SocketGuard.h"
#include "ui/Renderer.h"

// nxlink.h 里 __nxlink_host 用的 struct in_addr 只有前向声明，
// 要读它的成员必须先引入完整定义，所以这个头得排在 switch.h 前面。
#include <netinet/in.h>

#include <switch.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    bool g_enabled = false;
    bool g_socket_held = false;
    FILE* g_log_file = nullptr;
    int g_lines_since_commit = 0;

    const char* kDiagDir = "sdmc:/config/MusicPlayer2/diag";
    const char* kFlagPath = "sdmc:/config/MusicPlayer2/diag.flag";
    const char* kLegacyFlagPath = "sdmc:/switch/MusicPlayer2/diag.flag";
    const char* kLogPath = "sdmc:/config/MusicPlayer2/diag.log";

    // 待测文件名。按 UTF-8 编码长度分组：如果 2 字节能过而 3 字节不行，
    // 说明问题出在编码长度上；如果全部非 ASCII 都不行，那是另一回事。
    struct NameCase
    {
        const char* label;
        const char* name;
    };

    const NameCase kNameCases[] = {
        { "ASCII        ", "probe_ascii.txt" },
        { "拉丁补充(2字节)", "probe_üé.txt" },
        { "希腊字母(2字节)", "probe_Ωλ.txt" },
        { "中文    (3字节)", "probe_中文.txt" },
        { "日文假名(3字节)", "probe_かな.txt" },
        { "Emoji   (4字节)", "probe_\U0001f3b5.txt" },
    };

    bool HasNonAscii(const std::string& text)
    {
        for (unsigned char c : text)
        {
            if (c >= 0x80)
                return true;
        }
        return false;
    }

    std::string HexOf(const std::string& data)
    {
        static const char* kHex = "0123456789abcdef";
        std::string out;
        out.reserve(data.size() * 3);
        for (unsigned char c : data)
        {
            out += kHex[c >> 4];
            out += kHex[c & 0x0f];
            out += ' ';
        }
        return out;
    }
}

namespace Diag
{

bool IsEnabled()
{
    return g_enabled;
}

bool Begin()
{
    // 通过 nxlink 推送启动时 __nxlink_host 会被填上开发机地址
    bool via_nxlink = (__nxlink_host.s_addr != 0);
    // 新旧两个位置都认：让老的排查说明不至于突然失效
    bool has_flag = FileUtil::Exists(kFlagPath) || FileUtil::Exists(kLegacyFlagPath);
    if (!via_nxlink && !has_flag)
        return false;

    if (via_nxlink && SocketGuard::Acquire())
    {
        g_socket_held = true;
        if (nxlinkStdio() < 0)
        {
            // 拿不到远端终端就没必要占着 socket
            SocketGuard::Release();
            g_socket_held = false;
        }
    }

    // 同时写一份日志到 SD 卡。
    // nxlink 的 stdout 只有在从开发机推送启动时才有，而 netloader 和 ftpd
    // 不能同时开着；写文件则任何启动方式都能拿到结果，事后用 FTP 取走即可。
    //
    // 先删掉旧的：之前因为没有提交，卡上可能留下一个大小为 0、
    // 元数据损坏的条目，直接以 "wb" 打开它有可能失败。
    std::remove(kLogPath);
    FileUtil::CommitDevice(kLogPath);
    g_log_file = std::fopen(kLogPath, "wb");

    g_enabled = true;
    Logf("==================================================");
    Logf(" MusicPlayer2 for Switch - 诊断模式");
    Logf(" 触发方式: %s", via_nxlink ? "nxlink" : "diag.flag");
    Logf("==================================================");
    return true;
}

void End()
{
    Logf("---- 诊断结束 ----");
    if (g_log_file != nullptr)
    {
        std::fclose(g_log_file);
        g_log_file = nullptr;
        FileUtil::CommitDevice(kLogPath);
    }
    if (g_socket_held)
    {
        SocketGuard::Release();
        g_socket_held = false;
    }
    g_enabled = false;
}

void Logf(const char* format, ...)
{
    if (!g_enabled)
        return;

    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    std::printf("%s\n", line);
    std::fflush(stdout);

    if (g_log_file != nullptr)
    {
        std::fprintf(g_log_file, "%s\n", line);
        // 每行都刷盘：崩溃时最后几行恰恰是最有价值的
        std::fflush(g_log_file);

        // fflush 只是把数据交给 FS 服务，还要提交才会真正落到 SD 卡上。
        // 提交要刷整个 FAT，比较贵，所以攒一批再提交而不是每行都提交。
        if (++g_lines_since_commit >= 20)
        {
            FileUtil::CommitDevice(kLogPath);
            g_lines_since_commit = 0;
        }
    }
}

void LogBytes(const char* label, const std::string& data)
{
    if (!g_enabled)
        return;
    Logf("  %s: \"%s\"", label, data.c_str());
    Logf("      字节(%u): %s", static_cast<unsigned>(data.size()), HexOf(data).c_str());
}

void ProbeFileSystem()
{
    if (!g_enabled)
        return;

    Logf("");
    Logf("---- 文件系统 UTF-8 支持探测 ----");

    // 先确保诊断目录存在（纯 ASCII 名，作为基准）
    if (::mkdir(kDiagDir, 0777) != 0 && errno != EEXIST)
    {
        Logf("无法创建诊断目录 %s: errno=%d (%s)", kDiagDir, errno, std::strerror(errno));
        return;
    }
    FileUtil::CommitDevice(kDiagDir);

    std::vector<std::string> created;

    for (const NameCase& item : kNameCases)
    {
        std::string name = item.name;
        std::string path = std::string(kDiagDir) + "/" + name;

        errno = 0;
        FILE* fp = std::fopen(path.c_str(), "wb");
        if (fp == nullptr)
        {
            Logf("[写入失败] %s errno=%d (%s)", item.label, errno, std::strerror(errno));
            LogBytes("名字", name);
            continue;
        }
        std::fwrite("ok\n", 1, 3, fp);
        std::fclose(fp);
        // 真实代码写完都会提交，探针也必须提交，否则测的是"没提交"时的行为
        FileUtil::CommitDevice(kDiagDir);

        // 写进去只是第一步，还要能再打开、能被 stat 到才算真的支持
        errno = 0;
        struct stat st{};
        bool stat_ok = (::stat(path.c_str(), &st) == 0);
        int stat_errno = errno;

        errno = 0;
        FILE* rp = std::fopen(path.c_str(), "rb");
        bool reopen_ok = (rp != nullptr);
        int reopen_errno = errno;
        if (rp != nullptr)
            std::fclose(rp);

        std::string stat_desc = stat_ok ? "ok" : ("失败(errno=" + std::to_string(stat_errno) + ")");
        std::string reopen_desc = reopen_ok ? "ok" : ("失败(errno=" + std::to_string(reopen_errno) + ")");
        Logf("[写入成功] %s  stat=%s  重新打开=%s  大小=%lld",
             item.label, stat_desc.c_str(), reopen_desc.c_str(),
             static_cast<long long>(st.st_size));

        // 立刻回读一次目录：上一轮出现过"创建报成功、readdir 却看不到"的矛盾，
        // 每建一个就查一次才能看出到底是哪一步丢的，以及落盘的名字被改成了什么
        DIR* check = ::opendir(kDiagDir);
        if (check != nullptr)
        {
            int entries = 0;
            bool found_exact = false;
            struct dirent* e;
            while ((e = ::readdir(check)) != nullptr)
            {
                std::string entry_name = e->d_name;
                if (entry_name == "." || entry_name == "..")
                    continue;
                ++entries;
                if (entry_name == name)
                    found_exact = true;
            }
            ::closedir(check);
            Logf("           创建后立即 readdir：共 %d 项，按原名找到=%s",
                 entries, found_exact ? "是" : "否");
        }
        created.push_back(name);
    }

    // 目录名单独测一次：有些实现对文件名和目录名的处理并不一致
    {
        std::string dir = std::string(kDiagDir) + "/中文目录";
        errno = 0;
        bool ok = (::mkdir(dir.c_str(), 0777) == 0) || errno == EEXIST;
        FileUtil::CommitDevice(kDiagDir);
        std::string desc = ok ? "ok"
                              : ("失败 errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")");
        Logf("[目录] 中文目录名: %s", desc.c_str());
        if (ok)
            ::rmdir(dir.c_str());
    }

    // 回读：readdir 拿到的字节必须和写进去的一模一样，否则就是编码在中途被改了
    Logf("");
    Logf("---- readdir 回读 ----");
    DIR* dp = ::opendir(kDiagDir);
    if (dp == nullptr)
    {
        Logf("opendir 失败 errno=%d (%s)", errno, std::strerror(errno));
    }
    else
    {
        struct dirent* ent;
        while ((ent = ::readdir(dp)) != nullptr)
        {
            std::string name = ent->d_name;
            if (name == "." || name == "..")
                continue;
            LogBytes("条目", name);
        }
        ::closedir(dp);
    }

    // 清理，别在用户卡上留垃圾
    for (const std::string& name : created)
        ::remove((std::string(kDiagDir) + "/" + name).c_str());
    ::rmdir(kDiagDir);

    Logf("");
    Logf("---- 关键路径实测 ----");
    const char* kDirs[] = { "sdmc:/", "sdmc:/music", "sdmc:/switch", "sdmc:/config/MusicPlayer2" };
    for (const char* d : kDirs)
    {
        Logf("  %-28s 存在=%s 是目录=%s", d,
             FileUtil::Exists(d) ? "是" : "否",
             FileUtil::IsDirectory(d) ? "是" : "否");
    }

    // diag.flag 里每行写一个目录路径，就会被逐个枚举。
    // 这样要查哪个目录可以随时从 SD 卡上改，不用把用户的目录名写死在源码里。
    std::string flag_content;
    if (FileUtil::ReadAll(kFlagPath, flag_content)
        || FileUtil::ReadAll(kLegacyFlagPath, flag_content))
    {
        size_t start = 0;
        while (start < flag_content.size())
        {
            size_t end = flag_content.find('\n', start);
            if (end == std::string::npos)
                end = flag_content.size();
            std::string line = flag_content.substr(start, end - start);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.compare(0, 6, "sdmc:/") == 0)
                ProbeDirectory(line);
            start = end + 1;
        }
    }
}

void ProbeDirectory(const std::string& dir)
{
    if (!g_enabled)
        return;

    Logf("");
    Logf("---- 目录枚举: %s ----", dir.c_str());

    DIR* dp = ::opendir(dir.c_str());
    if (dp == nullptr)
    {
        Logf("opendir 失败 errno=%d (%s)", errno, std::strerror(errno));
        return;
    }

    int total = 0;
    int non_ascii = 0;
    int stat_failed = 0;
    int detail_budget = 24;             // 异常条目最多详细打这么多条，避免日志爆掉

    // 先数一遍条目总数。条目少就全部详细打印——非 ASCII 字符如果是被"删掉"
    // 而不是被替换成 '_'，剩下的名字看着完全正常，靠"可疑"规则会漏掉。
    int entry_count = 0;
    if (DIR* counter = ::opendir(dir.c_str()))
    {
        while (::readdir(counter) != nullptr)
            ++entry_count;
        ::closedir(counter);
    }
    const bool log_everything = (entry_count <= 40);

    struct dirent* ent;
    while ((ent = ::readdir(dp)) != nullptr)
    {
        std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;
        ++total;

        bool high = HasNonAscii(name);
        if (high)
            ++non_ascii;

        errno = 0;
        struct stat st{};
        bool ok = (::stat((dir + "/" + name).c_str(), &st) == 0);
        int err = errno;
        if (!ok)
            ++stat_failed;

        // 只详细记录"可疑"的条目：含非 ASCII 字节的，或者 stat 不到的。
        // 名字里带下划线的也算——如果非 ASCII 被替换成了 '_'，特征就在这里
        bool suspicious = log_everything || high || !ok
                          || name.find('_') != std::string::npos;
        if (suspicious && detail_budget > 0)
        {
            --detail_budget;
            LogBytes("条目", name);
            Logf("      stat=%s%s", ok ? "ok" : "失败",
                 ok ? "" : (" errno=" + std::to_string(err)).c_str());
        }
    }
    ::closedir(dp);

    Logf("小计：readdir 返回 %d 项，其中含非 ASCII 字节 %d 项，stat 失败 %d 项",
         total, non_ascii, stat_failed);
}

void ProbeNetwork(CCurlHttpClient& http)
{
    if (!g_enabled)
        return;

    Logf("");
    Logf("---- 网络与 TLS 自检 ----");
    // 时钟单独打出来：证书验证失败十有八九是这里的问题
    SystemClock::DateTime now = SystemClock::Now();
    Logf("  系统时间      : %s（读取%s）%s",
         SystemClock::FormatFull(now).c_str(), now.valid ? "成功" : "失败",
         SystemClock::IsClockImplausible() ? "  <== 与构建年份差距过大，很可能不准" : "");
    Logf("  构建年份      : %d", SystemClock::GetBuildYear());
    Logf("  网络可用      : %s", CCurlHttpClient::IsNetworkAvailable() ? "是" : "否");
    Logf("  curl 已初始化 : %s", http.IsInited() ? "是" : "否");
    Logf("  证书验证      : %s", http.IsCertVerified() ? "启用" : "未启用（找不到 CA 证书包）");

    const std::string& cert_path = http.GetCertPath();
    if (cert_path.empty())
    {
        Logf("  CA 证书包     : 未找到");
    }
    else
    {
        // 大小很关键：romfs 读取有问题的话这里会读出 0 或读不到，
        // 而 mbedTLS 拿到空的信任库同样会报"对端证书验证失败"，
        // 光看 curl 的错误码分不出这两种情况
        std::string content;
        bool read_ok = FileUtil::ReadAll(cert_path, content);
        Logf("  CA 证书包     : %s", cert_path.c_str());
        Logf("  能否读取      : %s，大小 %u 字节",
             read_ok ? "能" : "不能", static_cast<unsigned>(content.size()));
        if (read_ok)
        {
            size_t count = 0;
            for (size_t pos = content.find("BEGIN CERTIFICATE");
                 pos != std::string::npos;
                 pos = content.find("BEGIN CERTIFICATE", pos + 1))
            {
                ++count;
            }
            Logf("  含证书条目    : %u 个", static_cast<unsigned>(count));
        }
    }

    if (!http.IsInited() || !CCurlHttpClient::IsNetworkAvailable())
    {
        Logf("  跳过实际请求（网络未就绪）");
        return;
    }

    // 真的发一次更新检查用的请求，把结果原样记下来
    const char* kUrl = "https://api.github.com/repos/Schweik7/MusicPlayer2/releases/latest";
    HttpResponse response;
    std::vector<std::string> headers{ "Accept: application/vnd.github+json" };
    bool ok = http.Get(kUrl, headers, response);
    Logf("  GitHub API    : %s  HTTP %ld  响应 %u 字节",
         ok ? "成功" : "失败", response.status_code,
         static_cast<unsigned>(response.body.size()));
    if (!ok)
        Logf("  错误          : %s", response.error.c_str());
}

void ProbeFont(const CRenderer& renderer)
{
    if (!g_enabled)
        return;

    Logf("");
    Logf("---- 系统共享字体覆盖探测 ----");
    Logf("回退链长度(FS_NORMAL): %d", renderer.GetFontChainSize(CRenderer::FS_NORMAL));

    struct Sample { const char* label; char32_t cp; };
    const Sample kSamples[] = {
        { "A     U+0041", U'A' },
        { "中    U+4E2D", U'中' },
        { "文    U+6587", U'文' },
        { "音    U+97F3", U'音' },
        { "乐    U+4E50", U'乐' },
        { "周    U+5468", U'周' },
        { "杰    U+6770", U'杰' },
        { "伦    U+4F26", U'伦' },
        { "の    U+306E", U'の' },
        { "한    U+D55C", U'한' },
    };
    for (const Sample& s : kSamples)
    {
        int index = renderer.FindFontIndexForCodePoint(CRenderer::FS_NORMAL, s.cp);
        Logf("  %-16s 由回退链第 %d 个字体提供%s", s.label, index,
             index < 0 ? "  <== 缺字形!" : "");
    }
}

}   // namespace Diag
