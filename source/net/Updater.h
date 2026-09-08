#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class CCurlHttpClient;

// 从 GitHub Release 检查并安装新版本。
//
// 和 CDownloadManager 一样，网络操作全在后台线程，UI 每帧 Poll()。
// 这里用具体的 CCurlHttpClient 而不是 IHttpClient 接口：更新必须走
// DownloadToFile 并强制校验证书，那是 curl 实现才有的能力。
class CUpdater
{
public:
    enum State
    {
        ST_IDLE = 0,
        ST_CHECKING,
        ST_UP_TO_DATE,
        ST_UPDATE_AVAILABLE,
        ST_DOWNLOADING,
        ST_INSTALLED,
        ST_FAILED
    };

    struct Status
    {
        State state{ ST_IDLE };
        std::string message;
        std::string latest_version;
        std::string release_notes;
        std::string asset_url;
        uint64_t downloaded{};
        uint64_t total{};

        // 这次结果来自备用源而不是 GitHub
        bool from_mirror{};
        // 下载地址能否验证服务器身份（https 才能）。
        // 为 false 时安装的是一个来路无法确认的可执行文件，界面上必须说明。
        bool asset_verified{ true };
    };

    ~CUpdater();

    // self_path 是本程序 NRO 在 SD 卡上的位置，来自 main 的 argv[0]；
    // 取不到时退回默认路径。
    void Init(CCurlHttpClient* http, const std::string& self_path);

    bool IsBusy() const { return m_busy.load(); }
    bool StartCheck();
    // 必须先 StartCheck 并得到 ST_UPDATE_AVAILABLE 才能调用
    bool StartInstall();

    Status Poll() const;
    void Cancel();
    // 关闭网络栈之前必须调用，否则工作线程会用到已被清理的 curl
    void WaitForCompletion();
    void Reset();

    const std::string& GetSelfPath() const { return m_self_path; }

    // 比较两个版本号（形如 "0.4.0" 或 "v0.4.0"）。
    // 返回 true 表示 remote 比 local 新。抽成静态函数是为了能单独测。
    static bool IsNewerVersion(const std::string& remote, const std::string& local);

private:
    void RunTask(void (CUpdater::*task)());
    void JoinWorker();
    void DoCheck();
    // 向一个 Release 接口发查询并填好 m_status。成功返回 true。
    // GitHub 和备用源返回的 JSON 形状相同，所以这一段能共用。
    bool QueryRelease(const std::string& url, bool from_mirror, std::string& error);
    void DoInstall();
    void SetStatus(State state, const std::string& message);

    CCurlHttpClient* m_http{};
    std::string m_self_path{ "sdmc:/switch/MusicPlayer2.nro" };

    mutable std::mutex m_mutex;
    Status m_status;

    std::thread m_worker;
    std::atomic<bool> m_busy{ false };
    std::atomic<bool> m_cancel{ false };
};
