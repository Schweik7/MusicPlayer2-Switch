#include "DownloadManager.h"
#include "HttpClient.h"
#include "UrlUtil.h"
#include "../core/FileUtil.h"
#include "../core/StringUtil.h"

#include <cstdio>
#include <functional>

CDownloadManager::CDownloadManager()
{
    SetProvider(PROVIDER_NETEASE);
}

CDownloadManager::~CDownloadManager()
{
    Cancel();
    JoinWorker();
}

void CDownloadManager::SetProvider(ProviderId id)
{
    if (id < 0 || id >= PROVIDER_COUNT)
        id = PROVIDER_NETEASE;
    m_provider_id = id;
    if (id == PROVIDER_QQ)
        m_provider.reset(new CQQMusicProvider());
    else
        m_provider.reset(new CNeteaseProvider());
}

const char* CDownloadManager::GetProviderName() const
{
    return m_provider != nullptr ? m_provider->GetName() : "";
}

const char* CDownloadManager::GetProviderName(ProviderId id)
{
    return id == PROVIDER_QQ ? "QQ音乐" : "网易云音乐";
}

std::string CDownloadManager::GetLyricSavePath(const std::string& audio_file_path)
{
    return FileUtil::ReplaceExtension(audio_file_path, ".lrc");
}

std::string CDownloadManager::GetCoverSavePath(const std::string& audio_file_path,
                                               const std::string& image_url)
{
    // 与音频同名，这样 CRenderer::LoadCoverImage 第一顺位就能找到
    std::string ext = UrlUtil::GetExtensionFromUrl(image_url);
    if (ext != "png" && ext != "jpg" && ext != "jpeg")
        ext = "jpg";
    return FileUtil::ReplaceExtension(audio_file_path, "." + ext);
}

void CDownloadManager::SetStatus(State state, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.state = state;
    m_status.message = message;
}

CDownloadManager::Status CDownloadManager::Poll() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void CDownloadManager::Cancel()
{
    m_cancel.store(true);
}

void CDownloadManager::WaitForCompletion()
{
    m_cancel.store(true);
    JoinWorker();
}

void CDownloadManager::Reset()
{
    if (m_busy.load())
        return;
    JoinWorker();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = Status();
}

void CDownloadManager::JoinWorker()
{
    if (m_worker.joinable())
        m_worker.join();
}

void CDownloadManager::RunTask(std::function<void()> task)
{
    JoinWorker();                       // 上一个任务已经结束，回收它的线程
    m_cancel.store(false);
    m_busy.store(true);
    m_worker = std::thread([this, task]() {
        task();
        m_busy.store(false);
    });
}

bool CDownloadManager::StartSearch(const std::string& keyword, int result_count)
{
    if (m_busy.load() || m_http == nullptr || keyword.empty())
        return false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = Status();
        m_status.state = ST_SEARCHING;
        m_status.message = std::string("正在搜索：") + keyword;
    }

    RunTask([this, keyword, result_count]() { DoSearch(keyword, result_count); });
    return true;
}

void CDownloadManager::DoSearch(const std::string& keyword, int result_count)
{
    std::string url = m_provider->GetSearchUrl(keyword, result_count);
    std::vector<std::string> headers = m_provider->GetExtraHeaders();

    HttpResponse response;
    bool ok = m_provider->SearchUsesPost()
                  ? m_http->Post(url, std::string(), headers, response)
                  : m_http->Get(url, headers, response);

    if (m_cancel.load())
    {
        SetStatus(ST_IDLE, std::string());
        return;
    }
    if (!ok)
    {
        SetStatus(ST_FAILED, "搜索失败：" + response.error);
        return;
    }

    std::vector<DownloadItem> results;
    m_provider->ParseSearchResult(response.body, results);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.results = std::move(results);
    if (m_status.results.empty())
    {
        m_status.state = ST_FAILED;
        m_status.message = "没有搜索到匹配的歌曲";
    }
    else
    {
        m_status.state = ST_SEARCH_DONE;
        char buff[64];
        std::snprintf(buff, sizeof(buff), "找到 %d 个结果",
                      static_cast<int>(m_status.results.size()));
        m_status.message = buff;
    }
}

bool CDownloadManager::StartDownloadSelected(int index, const AutoRequest& request)
{
    if (m_busy.load() || m_http == nullptr)
        return false;
    if (!request.download_lyric && !request.download_cover)
        return false;               // 什么都不下载是调用方的错误，直接拒绝

    DownloadItem item;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index < 0 || index >= static_cast<int>(m_status.results.size()))
            return false;
        item = m_status.results[index];
        m_status.matched_index = index;
        m_status.state = ST_DOWNLOADING;
        m_status.message = "正在下载：" + item.GetDisplayName();
        m_status.saved_lyric_path.clear();
        m_status.saved_cover_path.clear();
    }

    RunTask([this, item, request]() { PerformDownloads(item, request); });
    return true;
}

void CDownloadManager::PerformDownloads(const DownloadItem& item, const AutoRequest& request)
{
    // 注意区分"没有请求"和"请求了但成功"：只有被请求的项才计入成败
    bool lyric_ok = request.download_lyric && DoDownloadLyric(item, request);
    bool cover_ok = request.download_cover && DoDownloadCover(item, request);

    if (m_cancel.load())
    {
        SetStatus(ST_IDLE, std::string());
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (lyric_ok || cover_ok)
    {
        m_status.state = ST_SUCCESS;
        std::string done;
        if (lyric_ok)
            done += "歌词";
        if (cover_ok)
            done += done.empty() ? "封面" : "、封面";
        m_status.message = done + "下载成功";
    }
    else
    {
        m_status.state = ST_FAILED;
        // DoDownloadXxx 失败时把原因写在了 message 里，这里只在没有原因时兜底
        if (m_status.message.empty() || StringUtil::StartsWith(m_status.message, "正在下载"))
            m_status.message = "下载失败";
    }
}

bool CDownloadManager::StartAutoDownload(const std::string& keyword, const AutoRequest& request)
{
    if (m_busy.load() || m_http == nullptr || keyword.empty())
        return false;
    if (!request.download_lyric && !request.download_cover)
        return false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = Status();
        m_status.state = ST_SEARCHING;
        m_status.message = std::string("正在搜索：") + keyword;
    }

    RunTask([this, keyword, request]() {
        DoSearch(keyword, 20);
        if (m_cancel.load())
        {
            SetStatus(ST_IDLE, std::string());
            return;
        }

        DownloadItem item;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_status.state != ST_SEARCH_DONE || m_status.results.empty())
                return;             // DoSearch 已经写好了失败原因

            std::string file_name = FileUtil::GetFileNameWithoutExt(request.audio_file_path);
            int matched = SongMatcher::SelectMatchedItem(m_status.results, request.title,
                                                         request.artist, request.album, file_name);
            if (matched < 0)
            {
                m_status.state = ST_FAILED;
                m_status.message = "没有找到足够匹配的结果，请手动选择";
                return;
            }
            m_status.matched_index = matched;
            item = m_status.results[matched];
            m_status.state = ST_DOWNLOADING;
            m_status.message = "正在下载：" + item.GetDisplayName();
        }

        PerformDownloads(item, request);
    });
    return true;
}

bool CDownloadManager::DoDownloadLyric(const DownloadItem& item, const AutoRequest& request)
{
    if (m_cancel.load())
        return false;

    std::string url = m_provider->GetLyricUrl(item.id, request.with_translation);
    HttpResponse response;
    if (!m_http->Get(url, m_provider->GetExtraHeaders(), response))
    {
        SetStatus(ST_DOWNLOADING, "歌词下载失败：" + response.error);
        return false;
    }

    std::string lyric;
    if (!m_provider->ParseLyric(response.body, request.with_translation, lyric))
    {
        SetStatus(ST_DOWNLOADING, "这首歌没有歌词");
        return false;
    }

    // 补上标签信息，与桌面版下载的歌词格式保持一致
    LyricProviderUtil::AddLyricTag(lyric, item.id,
                                   item.title.empty() ? request.title : item.title,
                                   item.artist.empty() ? request.artist : item.artist,
                                   item.album.empty() ? request.album : item.album);

    std::string save_path = GetLyricSavePath(request.audio_file_path);
    if (!FileUtil::WriteAll(save_path, lyric))
    {
        SetStatus(ST_DOWNLOADING, "歌词写入失败：" + save_path);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.saved_lyric_path = save_path;
    return true;
}

bool CDownloadManager::DoDownloadCover(const DownloadItem& item, const AutoRequest& request)
{
    if (m_cancel.load())
        return false;

    std::string info_url = m_provider->GetCoverInfoUrl(item.id);
    if (info_url.empty())
        return false;

    HttpResponse info_response;
    if (!m_http->Get(info_url, m_provider->GetExtraHeaders(), info_response))
    {
        SetStatus(ST_DOWNLOADING, "封面信息获取失败：" + info_response.error);
        return false;
    }

    std::string image_url = m_provider->ParseCoverUrl(info_response.body);
    if (image_url.empty())
    {
        SetStatus(ST_DOWNLOADING, "这首歌没有可用的封面");
        return false;
    }
    if (m_cancel.load())
        return false;

    std::string image_data;
    std::string error;
    if (!m_http->GetBinary(image_url, m_provider->GetExtraHeaders(), image_data, error))
    {
        SetStatus(ST_DOWNLOADING, "封面下载失败：" + error);
        return false;
    }
    if (image_data.size() < 128)        // 太小的响应基本是错误页而不是图片
    {
        SetStatus(ST_DOWNLOADING, "封面数据无效");
        return false;
    }

    std::string save_path = GetCoverSavePath(request.audio_file_path, image_url);
    if (!FileUtil::WriteAll(save_path, image_data))
    {
        SetStatus(ST_DOWNLOADING, "封面写入失败：" + save_path);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.saved_cover_path = save_path;
    return true;
}
