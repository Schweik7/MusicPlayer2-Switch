#include "HttpClient.h"
#include "SocketGuard.h"
#include "../SystemClock.h"
#include "../core/FileUtil.h"

#include <switch.h>
#include <curl/curl.h>

#include <cstdio>
#include "../core/Lang.h"

namespace
{
    // 单次响应的体积上限，防止异常响应把内存吃光。
    // 搜索/歌词接口的响应都在几十 KB 量级，封面图片给到 8MB。
    const size_t kMaxTextBytes = 4 * 1024 * 1024;
    const size_t kMaxImageBytes = 8 * 1024 * 1024;

    struct WriteContext
    {
        std::string* buffer;
        size_t max_bytes;
        bool overflowed;
    };

    size_t WriteCallback(char* data, size_t size, size_t nmemb, void* userdata)
    {
        WriteContext* ctx = static_cast<WriteContext*>(userdata);
        size_t total = size * nmemb;
        if (ctx->buffer->size() + total > ctx->max_bytes)
        {
            ctx->overflowed = true;
            return 0;                           // 返回 0 让 curl 以写入错误中断传输
        }
        ctx->buffer->append(data, total);
        return total;
    }
}


namespace
{
    // 证书验证失败最常见的原因其实是系统时间不对：时间偏得远了，
    // 证书会被判成尚未生效或已过期，而 curl 只会报一句"证书验证失败"，
    // 用户根本联想不到时钟。这里主动把这层意思补出来。
    std::string TlsFailureHint(int curl_code)
    {
        const int kPeerFailedVerification = 60;
        const int kCaCertBadFile = 77;
        if (curl_code == kCaCertBadFile)
            return T("（CA 证书包读不出来）");
        if (curl_code != kPeerFailedVerification)
            return std::string();
        if (!SystemClock::IsClockImplausible())
            return std::string();
        SystemClock::DateTime now = SystemClock::Now();
        return T("（系统时间为 ") + SystemClock::FormatFull(now)
             + T("，证书可能因此被判为过期，请先校准主机时间）");
    }
}

CCurlHttpClient::~CCurlHttpClient()
{
    Uninit();
}

bool CCurlHttpClient::IsNetworkAvailable()
{
    NifmInternetConnectionType type{};
    u32 strength = 0;
    NifmInternetConnectionStatus status{};
    if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status)))
        return false;
    return status == NifmInternetConnectionStatus_Connected;
}

void CCurlHttpClient::LocateCaBundle()
{
    // 优先用户放在 SD 卡上的证书包（方便更新），其次是打进 romfs 的那份
    const char* candidates[] = {
        "sdmc:/config/MusicPlayer2/cacert.pem",
        "sdmc:/switch/MusicPlayer2/cacert.pem",       // 0.6.2 之前的位置
        "romfs:/cacert.pem",
    };
    for (const char* path : candidates)
    {
        if (FileUtil::Exists(path))
        {
            m_cert_path = path;
            m_cert_verified = true;
            return;
        }
    }
    m_cert_path.clear();
    m_cert_verified = false;
}

bool CCurlHttpClient::Init()
{
    if (m_inited)
        return true;

    // 走引用计数：nxlink 的 stdout 重定向可能已经把 socket 初始化过了
    if (!SocketGuard::Acquire())
        return false;
    m_socket_inited = true;

    if (R_FAILED(nifmInitialize(NifmServiceType_User)))
    {
        SocketGuard::Release();
        m_socket_inited = false;
        return false;
    }
    m_nifm_inited = true;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        nifmExit();
        SocketGuard::Release();
        m_nifm_inited = false;
        m_socket_inited = false;
        return false;
    }

    LocateCaBundle();
    m_inited = true;
    return true;
}

void CCurlHttpClient::Uninit()
{
    if (!m_inited)
        return;
    curl_global_cleanup();
    if (m_nifm_inited)
    {
        nifmExit();
        m_nifm_inited = false;
    }
    if (m_socket_inited)
    {
        SocketGuard::Release();
        m_socket_inited = false;
    }
    m_inited = false;
}

bool CCurlHttpClient::Perform(const std::string& url, const std::string* post_body,
                              const std::vector<std::string>& headers, HttpResponse& out,
                              size_t max_bytes)
{
    out = HttpResponse();

    if (!m_inited)
    {
        out.error = T("网络未初始化");
        return false;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        out.error = T("curl_easy_init 失败");
        return false;
    }

    WriteContext ctx{ &out.body, max_bytes, false };

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(m_timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");        // 允许 gzip，由 curl 自动解压
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) MusicPlayer2-Switch");

    if (m_cert_verified)
    {
        curl_easy_setopt(curl, CURLOPT_CAINFO, m_cert_path.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    else
    {
        // 没有 CA 证书包时无法验证服务器身份。这里选择继续连接而不是直接失败，
        // 但上层会把这个状态显示给用户（见 DownloadManager 的提示）。
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    struct curl_slist* header_list = nullptr;
    for (const std::string& header : headers)
        header_list = curl_slist_append(header_list, header.c_str());
    if (header_list != nullptr)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

    if (post_body != nullptr)
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_body->size()));
    }

    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status_code);

    if (header_list != nullptr)
        curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK)
    {
        if (ctx.overflowed)
        {
            out.error = T("响应内容过大");
        }
        else
        {
            // 带上错误码：证书类问题里 60（对端证书验证失败）和
            // 77（CA 文件读不出来）原因完全不同，只看文案分不出来
            char buff[32];
            std::snprintf(buff, sizeof(buff), "[curl %d] ", static_cast<int>(code));
            out.error = std::string(buff) + curl_easy_strerror(code)
                      + TlsFailureHint(static_cast<int>(code));
        }
        out.body.clear();
        return false;
    }
    if (out.status_code < 200 || out.status_code >= 300)
    {
        char buff[64];
        std::snprintf(buff, sizeof(buff), T("服务器返回 HTTP %ld"), out.status_code);
        out.error = buff;
        return false;
    }

    out.success = true;
    return true;
}

namespace
{
    struct FileWriteContext
    {
        FILE* fp;
        uint64_t written;
        uint64_t total;
        const CCurlHttpClient::ProgressCallback* progress;
        bool cancelled;
        bool write_failed;
    };

    size_t FileWriteCallback(char* data, size_t size, size_t nmemb, void* userdata)
    {
        FileWriteContext* ctx = static_cast<FileWriteContext*>(userdata);
        size_t total = size * nmemb;
        if (std::fwrite(data, 1, total, ctx->fp) != total)
        {
            ctx->write_failed = true;              // SD 卡写满或拔卡
            return 0;
        }
        ctx->written += total;
        if (ctx->progress != nullptr && *ctx->progress
            && !(*ctx->progress)(ctx->written, ctx->total))
        {
            ctx->cancelled = true;
            return 0;
        }
        return total;
    }

    int ProgressMetaCallback(void* userdata, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t, curl_off_t)
    {
        FileWriteContext* ctx = static_cast<FileWriteContext*>(userdata);
        if (dltotal > 0)
            ctx->total = static_cast<uint64_t>(dltotal);
        (void)dlnow;
        return ctx->cancelled ? 1 : 0;
    }
}

bool CCurlHttpClient::DownloadToFile(const std::string& url,
                                     const std::vector<std::string>& headers,
                                     const std::string& dest_path, bool require_cert,
                                     const ProgressCallback& progress, std::string& error)
{
    error.clear();
    if (!m_inited)
    {
        error = T("网络未初始化");
        return false;
    }
    // 下载下来会被当程序执行的东西，绝不能在无法验证服务器身份的情况下取
    if (require_cert && !m_cert_verified)
    {
        error = T("缺少 CA 证书包，无法验证服务器身份");
        return false;
    }

    FILE* fp = std::fopen(dest_path.c_str(), "wb");
    if (fp == nullptr)
    {
        error = T("无法写入 ") + dest_path;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        std::fclose(fp);
        std::remove(dest_path.c_str());
        error = T("curl_easy_init 失败");
        return false;
    }

    FileWriteContext ctx{ fp, 0, 0, &progress, false, false };

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressMetaCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    // 大文件不设总时长上限，改用"低速多久算超时"，否则网慢就永远下不完
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MusicPlayer2-Switch");

    if (m_cert_verified)
    {
        curl_easy_setopt(curl, CURLOPT_CAINFO, m_cert_path.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    else
    {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    struct curl_slist* header_list = nullptr;
    for (const std::string& header : headers)
        header_list = curl_slist_append(header_list, header.c_str());
    if (header_list != nullptr)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (header_list != nullptr)
        curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    // fclose 的返回值必须看：缓冲区最后一次刷盘就发生在这里。
    // 忽略它的话，写失败会伪装成一次成功的下载，留下一个半截文件。
    const bool close_ok = (std::fclose(fp) == 0);
    // 关文件之后必须提交，否则大文件在 SD 卡上是个空壳
    FileUtil::CommitDevice(dest_path);

    bool ok = close_ok && (code == CURLE_OK) && status >= 200 && status < 300;
    if (!ok)
    {
        if (ctx.cancelled)
            error = T("已取消");
        else if (ctx.write_failed || !close_ok)
            error = T("写入 SD 卡失败（卡满或写入出错）");
        else if (code != CURLE_OK)
        {
            char buff[32];
            std::snprintf(buff, sizeof(buff), "[curl %d] ", static_cast<int>(code));
            error = std::string(buff) + curl_easy_strerror(code)
                  + TlsFailureHint(static_cast<int>(code));
        }
        else
        {
            char buff[64];
            std::snprintf(buff, sizeof(buff), T("服务器返回 HTTP %ld"), status);
            error = buff;
        }
        std::remove(dest_path.c_str());         // 别把半截文件留在卡上
        return false;
    }

    // 服务器报了长度就核对一下，截断的下载绝不能拿去替换程序
    if (ctx.total > 0 && ctx.written != ctx.total)
    {
        char buff[96];
        std::snprintf(buff, sizeof(buff), T("下载不完整（收到 %llu / %llu 字节）"),
                      static_cast<unsigned long long>(ctx.written),
                      static_cast<unsigned long long>(ctx.total));
        error = buff;
        std::remove(dest_path.c_str());
        return false;
    }

    // 再核对一次落到卡上的实际大小。
    //
    // 上面那个检查看的是"交给 fwrite 多少字节"，这里看的是"卡上真有多少字节"，
    // 两者不是一回事：写入缓冲、提交失败、卡满都会让后者小于前者。
    // 用户遇到过一次 10.9MB 的更新在卡上只剩 2MB，而下载本身报的是成功。
    const uint64_t on_disk = FileUtil::GetFileSize(dest_path);
    if (on_disk != ctx.written)
    {
        char buff[112];
        std::snprintf(buff, sizeof(buff), T("写入 SD 卡不完整（卡上 %llu / 应有 %llu 字节）"),
                      static_cast<unsigned long long>(on_disk),
                      static_cast<unsigned long long>(ctx.written));
        error = buff;
        std::remove(dest_path.c_str());
        return false;
    }
    return true;
}

bool CCurlHttpClient::Get(const std::string& url, const std::vector<std::string>& headers,
                          HttpResponse& out)
{
    return Perform(url, nullptr, headers, out, kMaxTextBytes);
}

bool CCurlHttpClient::Post(const std::string& url, const std::string& body,
                           const std::vector<std::string>& headers, HttpResponse& out)
{
    return Perform(url, &body, headers, out, kMaxTextBytes);
}

bool CCurlHttpClient::GetBinary(const std::string& url, const std::vector<std::string>& headers,
                                std::string& out, std::string& error)
{
    HttpResponse response;
    if (!Perform(url, nullptr, headers, response, kMaxImageBytes))
    {
        error = response.error;
        out.clear();
        return false;
    }
    out = std::move(response.body);
    return true;
}
