#include "LibraryScanner.h"
#include "MediaScanner.h"

CLibraryScanner::~CLibraryScanner()
{
    WaitForCompletion();
}

void CLibraryScanner::JoinWorker()
{
    if (m_worker.joinable())
        m_worker.join();
}

void CLibraryScanner::WaitForCompletion()
{
    m_cancel.store(true);
    JoinWorker();
    m_cancel.store(false);
}

void CLibraryScanner::Cancel()
{
    m_cancel.store(true);
}

bool CLibraryScanner::Start(const std::string& dir, int max_depth)
{
    if (m_busy.load())
        return false;

    JoinWorker();                       // 回收上一次已经结束的线程
    m_cancel.store(false);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_progress = Progress();
        m_progress.running = true;
        m_progress.current_dir = dir;
        m_result.clear();
    }
    m_busy.store(true);
    m_worker = std::thread([this, dir, max_depth]() {
        Run(dir, max_depth);
        m_busy.store(false);
    });
    return true;
}

void CLibraryScanner::Run(std::string dir, int max_depth)
{
    std::vector<SongInfo> songs;
    CMediaScanner::ScanDirectory(dir, songs, max_depth, &m_cancel);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_progress.running = false;
    if (m_cancel.load())
    {
        // 被取消：结果丢弃，也不标记完成，免得调用方拿到半截列表
        m_progress.finished = false;
        m_progress.found = 0;
        return;
    }
    m_progress.finished = true;
    m_progress.found = static_cast<int>(songs.size());
    m_result = std::move(songs);
}

CLibraryScanner::Progress CLibraryScanner::Poll() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_progress;
}

bool CLibraryScanner::TakeResult(std::vector<SongInfo>& out)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_progress.finished)
        return false;
    out = std::move(m_result);
    m_result.clear();
    m_progress = Progress();
    return true;
}
