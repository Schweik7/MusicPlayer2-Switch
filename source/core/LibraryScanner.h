#pragma once
#include "SongInfo.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 后台扫描音乐库。
//
// 逐个文件读标签让扫描明显变慢（647 首约 10 秒），放在启动路径上会让程序
// 十秒钟没有响应。这里把扫描挪到后台线程，界面立刻可用，扫完再把结果交上去。
//
// 只负责"扫描"这一件事：结果怎么用（是否替换当前播放列表）由调用方决定，
// 因为用户可能在扫描期间已经自己选了别的歌。
class CLibraryScanner
{
public:
    struct Progress
    {
        bool running{};
        bool finished{};        // 有一次扫描已完成且结果尚未被取走
        int found{};            // 已找到的曲目数
        std::string current_dir;
    };

    ~CLibraryScanner();

    // 已在扫描时返回 false
    bool Start(const std::string& dir, int max_depth = 3);
    void Cancel();
    // 阻塞到工作线程退出。析构和程序退出前必须调用。
    void WaitForCompletion();

    Progress Poll() const;

    // 取走扫描结果并把状态清回空闲。没有已完成的结果时返回 false。
    bool TakeResult(std::vector<SongInfo>& out);

private:
    void Run(std::string dir, int max_depth);
    void JoinWorker();

    mutable std::mutex m_mutex;
    Progress m_progress;
    std::vector<SongInfo> m_result;

    std::thread m_worker;
    std::atomic<bool> m_busy{ false };
    std::atomic<bool> m_cancel{ false };
};
