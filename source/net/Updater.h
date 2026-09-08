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
    // 从哪里取更新。和 CConfig::UpdateSource 一一对应。
    enum Source
    {
        SRC_AUTO = 0,       // 先试 GitHub，失败再走备用源
        SRC_GITHUB,
        SRC_MIRROR
    };

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
        uint64_t total{};           // 进度条用，下载中会被 curl 报的值覆盖
        // Release 里声明的资产大小。和 total 分开存：total 会被进度回调改写，
        // 也会在开始下载时清零，拿它当校验基准会让校验形同虚设。
        uint64_t asset_size{};

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
    // source 决定这次去哪里查，安装时沿用同一个选择
    bool StartCheck(Source source = SRC_AUTO);
    // 必须先 StartCheck 并得到 ST_UPDATE_AVAILABLE 才能调用
    bool StartInstall();

    // 启动时调用一次：把上次下载好的新版本换上去。
    //
    // 为什么不在下载完当场换：实机上替换正在运行的 NRO 一直失败，而那个流程
    // 需要先把自己改名挪走，中途失败时程序位置上是空的。启动时这一份镜像
    // 已经读进内存了，覆盖磁盘文件不影响当前进程，风险小得多。
    // 返回一句给用户看的话；没有待应用的更新时返回空串。
    std::string ApplyPendingUpdate();

    Status Poll() const;
    void Cancel();
    // 关闭网络栈之前必须调用，否则工作线程会用到已被清理的 curl
    void WaitForCompletion();
    void Reset();

    const std::string& GetSelfPath() const { return m_self_path; }
    // 下载好但还没应用的新版本放在哪
    std::string GetPendingPath() const { return m_self_path + ".new"; }

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
    // 替换失败时把备份放回程序位置。放不回去就在状态里说明备份在哪，
    // 那种情况下用户手上是没有可运行的 NRO 的，必须给出可操作的下一步。
    void RestoreBackup(bool had_old, const std::string& backup_path);
    void SetStatus(State state, const std::string& message);

    CCurlHttpClient* m_http{};
    // 只在 StartCheck 里写、工作线程里读；两者之间隔着一次线程创建，
    // 不需要额外同步。
    Source m_source{ SRC_AUTO };
    std::string m_self_path{ "sdmc:/switch/MusicPlayer2.nro" };

    mutable std::mutex m_mutex;
    Status m_status;

    std::thread m_worker;
    std::atomic<bool> m_busy{ false };
    std::atomic<bool> m_cancel{ false };
};
