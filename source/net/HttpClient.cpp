#include "HttpClient.h"
#include "../core/FileUtil.h"

#include <switch.h>
#include <curl/curl.h>

#include <cstdio>

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
        "sdmc:/switch/MusicPlayer2/cacert.pem",
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

    if (R_FAILED(socketInitializeDefault()))
        return false;
    m_socket_inited = true;

    if (R_FAILED(nifmInitialize(NifmServiceType_User)))
    {
        socketExit();
        m_socket_inited = false;
        return false;
    }
    m_nifm_inited = true;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        nifmExit();
        socketExit();
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
        socketExit();
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
        out.error = "网络未初始化";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        out.error = "curl_easy_init 失败";
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
            out.error = "响应内容过大";
        else
            out.error = curl_easy_strerror(code);
        out.body.clear();
        return false;
    }
    if (out.status_code < 200 || out.status_code >= 300)
    {
        char buff[64];
        std::snprintf(buff, sizeof(buff), "服务器返回 HTTP %ld", out.status_code);
        out.error = buff;
        return false;
    }

    out.success = true;
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
