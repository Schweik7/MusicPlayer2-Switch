#pragma once
#include "LyricProvider.h"
#include "SongMatcher.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class IHttpClient;

// 在线歌词/封面下载的任务调度。
//
// Switch 上的网络请求可能要几秒才回来，直接在主循环里做会让界面卡死，
// 因此所有网络操作都跑在一个后台线程上，UI 线程每帧调用 Poll() 取状态。
class CDownloadManager
{
public:
    enum State
    {
        ST_IDLE = 0,
        ST_SEARCHING,
        ST_SEARCH_DONE,
        ST_DOWNLOADING,
        ST_SUCCESS,
        ST_FAILED
    };

    enum ProviderId
    {
        PROVIDER_NETEASE = 0,
        PROVIDER_QQ,
        PROVIDER_COUNT
    };

    // 一次自动下载（搜索 + 自动选最匹配项 + 下载）的输入
    struct AutoRequest
    {
        std::string audio_file_path;    // 决定歌词和封面的保存位置
        std::string title;
        std::string artist;
        std::string album;
        // 本地文件时长（毫秒），0 表示未知。
        // 用来在同名的不同版本（Live / 伴奏 / 加长版）之间挑对的那个。
        int duration_ms{};
        bool download_lyric{ true };
        bool download_cover{ true };
        bool with_translation{ true };
    };

    // 一次快照，供 UI 线程读取
    struct Status
    {
        State state{ ST_IDLE };
        std::string message;            // 给用户看的一句话
        std::vector<DownloadItem> results;
        int matched_index{ -1 };        // 自动模式下选中的项
        std::string saved_lyric_path;
        std::string saved_cover_path;
    };

    CDownloadManager();
    ~CDownloadManager();

    // http_client 的生命周期由调用方负责，必须长于本对象的使用期
    void SetHttpClient(IHttpClient* client) { m_http = client; }

    void SetProvider(ProviderId id);
    ProviderId GetProvider() const { return m_provider_id; }
    const char* GetProviderName() const;
    static const char* GetProviderName(ProviderId id);

    bool IsBusy() const { return m_busy.load(); }

    // 以下三个入口在已有任务运行时会被忽略并返回 false
    bool StartSearch(const std::string& keyword, int result_count = 20);
    // 下载 results[index] 对应的歌词/封面到 audio_file_path 旁边
    bool StartDownloadSelected(int index, const AutoRequest& request);
    // 搜索 + 自动匹配 + 下载，一步到位
    bool StartAutoDownload(const std::string& keyword, const AutoRequest& request);

    // UI 线程每帧调用，返回当前状态的快照
    Status Poll() const;
    // 请求取消。正在进行的 HTTP 请求不会被立刻打断，但结果会被丢弃。
    void Cancel();
    // 阻塞到后台线程真正退出为止。
    // 关闭网络栈之前必须调用，否则工作线程可能在 curl 已被清理后继续使用它。
    void WaitForCompletion();
    // 把状态清回 ST_IDLE（用户看过结果之后）
    void Reset();

    // 歌词/封面的保存路径，供 UI 提示与测试使用
    static std::string GetLyricSavePath(const std::string& audio_file_path);
    static std::string GetCoverSavePath(const std::string& audio_file_path,
                                        const std::string& image_url);

private:
    void RunTask(std::function<void()> task);
    void JoinWorker();

    void DoSearch(const std::string& keyword, int result_count);
    // 执行请求的下载项并写入最终状态。
    // "没有请求"和"下载成功"必须区分开：只要有一项被请求且失败、而没有任何请求项成功，
    // 整体就是失败。
    void PerformDownloads(const DownloadItem& item, const AutoRequest& request);
    // 返回是否成功；失败原因写进 m_status.message
    bool DoDownloadLyric(const DownloadItem& item, const AutoRequest& request);
    bool DoDownloadCover(const DownloadItem& item, const AutoRequest& request);

    void SetStatus(State state, const std::string& message);

    IHttpClient* m_http{};
    ProviderId m_provider_id{ PROVIDER_NETEASE };
    std::unique_ptr<ILyricProvider> m_provider;

    mutable std::mutex m_mutex;
    Status m_status;

    std::thread m_worker;
    std::atomic<bool> m_busy{ false };
    std::atomic<bool> m_cancel{ false };
};
