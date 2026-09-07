#pragma once
#include <string>
#include <vector>

struct HttpResponse
{
    bool success{};
    long status_code{};
    std::string body;
    std::string error;              // success 为 false 时说明原因
};

// HTTP 客户端接口。抽出来是为了让上层的下载流程可以在开发机上用假实现测试，
// 真机上由 CCurlHttpClient 提供。
class IHttpClient
{
public:
    virtual ~IHttpClient() = default;

    virtual bool Get(const std::string& url, const std::vector<std::string>& headers,
                     HttpResponse& out) = 0;
    virtual bool Post(const std::string& url, const std::string& body,
                      const std::vector<std::string>& headers, HttpResponse& out) = 0;

    // 下载二进制内容（封面图片）
    virtual bool GetBinary(const std::string& url, const std::vector<std::string>& headers,
                           std::string& out, std::string& error) = 0;
};

// 基于 libcurl 的实现。只在 Switch 上编译（依赖 libnx 的网络服务）。
class CCurlHttpClient : public IHttpClient
{
public:
    ~CCurlHttpClient() override;

    // 初始化 libnx 的 socket / nifm 与 curl 全局状态
    bool Init();
    void Uninit();
    bool IsInited() const { return m_inited; }

    // 是否验证了服务器证书。没找到 CA 证书包时会退化为不验证，
    // 上层据此给用户一个明确的提示，而不是默默地做不安全的连接。
    bool IsCertVerified() const { return m_cert_verified; }
    const std::string& GetCertPath() const { return m_cert_path; }

    // 网络是否可用（nifm 报告的连接状态）
    static bool IsNetworkAvailable();

    bool Get(const std::string& url, const std::vector<std::string>& headers,
             HttpResponse& out) override;
    bool Post(const std::string& url, const std::string& body,
              const std::vector<std::string>& headers, HttpResponse& out) override;
    bool GetBinary(const std::string& url, const std::vector<std::string>& headers,
                   std::string& out, std::string& error) override;

    void SetTimeoutSeconds(int seconds) { m_timeout_seconds = seconds; }

private:
    // post_body 为 nullptr 时发 GET
    bool Perform(const std::string& url, const std::string* post_body,
                 const std::vector<std::string>& headers, HttpResponse& out, size_t max_bytes);

    void LocateCaBundle();

    bool m_inited{};
    bool m_socket_inited{};
    bool m_nifm_inited{};
    bool m_cert_verified{};
    std::string m_cert_path;
    int m_timeout_seconds{ 15 };
};
