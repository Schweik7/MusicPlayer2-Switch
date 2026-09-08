#include "Updater.h"
#include "HttpClient.h"
#include "ReleaseInfo.h"
#include "../Version.h"
#include "../core/FileUtil.h"
#include "../core/StringUtil.h"
#include "../core/VersionUtil.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
    const char* kApiUrl =
        "https://api.github.com/repos/" MP2_SWITCH_REPO_OWNER "/" MP2_SWITCH_REPO_NAME
        "/releases/latest";

    // NRO 的魔数在文件偏移 0x10 处。校验它是为了挡住"下到一个 HTML 错误页
    // 然后把它当程序装上去"这种情况——那会让程序再也起不来。
    bool LooksLikeNro(const std::string& path)
    {
        FILE* fp = std::fopen(path.c_str(), "rb");
        if (fp == nullptr)
            return false;
        char magic[4] = {};
        bool ok = (std::fseek(fp, 0x10, SEEK_SET) == 0)
                  && (std::fread(magic, 1, 4, fp) == 4);
        std::fclose(fp);
        return ok && magic[0] == 'N' && magic[1] == 'R' && magic[2] == 'O' && magic[3] == '0';
    }

    // "先试改名、不行就复制后删源"这套退路挪到了 FileUtil::MoveOverwrite：
    // 标签写入也要"写临时文件再顶替原文件"，同一件事不该有两份实现。
}

bool CUpdater::IsNewerVersion(const std::string& remote, const std::string& local)
{
    return VersionUtil::IsNewer(remote, local);
}

CUpdater::~CUpdater()
{
    WaitForCompletion();
}

void CUpdater::Init(CCurlHttpClient* http, const std::string& self_path)
{
    m_http = http;
    SetSelfPath(self_path);
}

void CUpdater::SetSelfPath(const std::string& self_path)
{
    if (!self_path.empty())
        m_self_path = self_path;
}

void CUpdater::SetStatus(State state, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.state = state;
    m_status.message = message;
}

CUpdater::Status CUpdater::Poll() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void CUpdater::Cancel()
{
    m_cancel.store(true);
}

void CUpdater::JoinWorker()
{
    if (m_worker.joinable())
        m_worker.join();
}

void CUpdater::WaitForCompletion()
{
    m_cancel.store(true);
    JoinWorker();
    m_cancel.store(false);
}

void CUpdater::Reset()
{
    if (m_busy.load())
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = Status();
}

void CUpdater::RunTask(void (CUpdater::*task)())
{
    JoinWorker();                           // 回收上一次已结束的线程
    m_cancel.store(false);
    m_busy.store(true);
    m_worker = std::thread([this, task]() {
        (this->*task)();
        m_busy.store(false);
    });
}

bool CUpdater::StartCheck(Source source)
{
    if (m_busy.load() || m_http == nullptr)
        return false;
    m_source = source;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = Status();
        m_status.state = ST_CHECKING;
        m_status.message = "正在检查更新…";
    }
    RunTask(&CUpdater::DoCheck);
    return true;
}

bool CUpdater::StartInstall()
{
    if (m_busy.load() || m_http == nullptr)
        return false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_status.state != ST_UPDATE_AVAILABLE || m_status.asset_url.empty())
            return false;
        m_status.state = ST_DOWNLOADING;
        m_status.message = "正在下载新版本…";
        m_status.downloaded = 0;
        m_status.total = 0;
    }
    RunTask(&CUpdater::DoInstall);
    return true;
}

bool CUpdater::QueryRelease(const std::string& url, bool from_mirror, std::string& error)
{
    HttpResponse response;
    std::vector<std::string> headers{
        "Accept: application/vnd.github+json",
        "X-GitHub-Api-Version: 2022-11-28",
    };
    if (!m_http->Get(url, headers, response))
    {
        error = response.error;
        return false;
    }

    ReleaseInfo::Info info;
    if (!ReleaseInfo::Parse(response.body, MP2_SWITCH_ASSET_NAME, info, error))
        return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.latest_version = info.tag;
    m_status.release_notes = info.notes;
    m_status.asset_url = info.asset_url;
    m_status.total = info.asset_size;
    m_status.asset_size = info.asset_size;
    m_status.from_mirror = from_mirror;
    // 只有 https 才能确认对面是谁。备用源现在走的是明文 http，
    // 那种情况下安装的是一个来路无法确认的可执行文件，得让用户知道。
    m_status.asset_verified = StringUtil::StartsWith(info.asset_url, "https://");
    return true;
}

void CUpdater::DoCheck()
{
    std::string error;
    bool ok = false;

    // 指定了只用备用源就不去碰 GitHub。
    // GitHub 在部分地区要等到超时才失败，那十几秒是白等的——
    // 知道自己连不上的用户应该能直接跳过它。
    if (m_source == SRC_MIRROR)
    {
        ok = QueryRelease(MP2_SWITCH_MIRROR_URL, true, error);
    }
    else
    {
        ok = QueryRelease(kApiUrl, false, error);
        // 只在"没拿到结果"时回退：拿到了但版本更旧不算失败
        if (!ok && m_source == SRC_AUTO && !m_cancel.load())
        {
            std::string mirror_error;
            if (QueryRelease(MP2_SWITCH_MIRROR_URL, true, mirror_error))
                ok = true;
            else
                error += "；备用源也失败：" + mirror_error;
        }
    }

    if (m_cancel.load())
        return;
    if (!ok)
    {
        SetStatus(ST_FAILED, "检查更新失败：" + error);
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string source = m_status.from_mirror ? "（备用源）" : "";

    if (!IsNewerVersion(m_status.latest_version, MP2_SWITCH_VERSION))
    {
        m_status.state = ST_UP_TO_DATE;
        m_status.message = "已是最新版本（" MP2_SWITCH_VERSION "）" + source;
    }
    else if (m_status.asset_url.empty())
    {
        // 有新版本但没带 NRO，只能让用户自己去下
        m_status.state = ST_FAILED;
        m_status.message = "发现 " + m_status.latest_version
                         + "，但该版本没有附带 " MP2_SWITCH_ASSET_NAME;
    }
    else
    {
        m_status.state = ST_UPDATE_AVAILABLE;
        m_status.message = "发现新版本 " + m_status.latest_version + source;
    }
}

void CUpdater::RestoreBackup(bool had_old, const std::string& backup_path)
{
    if (!had_old)
        return;
    // 还原失败是最坏的情况：程序位置上没有可用的 NRO 了。
    // 那就把备份留在原地并告诉用户它在哪，至少能手动改回来。
    if (!FileUtil::MoveOverwrite(backup_path, m_self_path))
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.message = "还原失败，原版本仍在 " + backup_path + "，请手动改回 "
                         + m_self_path;
    }
}

void CUpdater::DoInstall()
{
    std::string url;
    bool verified = true;
    bool from_mirror = false;
    uint64_t expected_size = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_status.asset_url;
        verified = m_status.asset_verified;
        from_mirror = m_status.from_mirror;
        expected_size = m_status.asset_size;    // Release 里声明的大小，不受进度回调影响
    }

    const std::string temp_path = m_self_path + ".new";
    const std::string backup_path = m_self_path + ".bak";

    auto progress = [this](uint64_t downloaded, uint64_t total) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.downloaded = downloaded;
        if (total > 0)
            m_status.total = total;
        return !m_cancel.load();
    };

    std::string error;
    // 会被执行的内容，能验证就一定要验。
    // 非 https 的地址验不了，那种情况下 verified 为 false（见 QueryRelease）。
    bool ok = m_http->DownloadToFile(url, {}, temp_path, verified, progress, error);

    // 下载失败也要回退到备用源。
    //
    // 原来只有"检查更新"会回退，下载不会——但 GitHub 的资产下载走的是另一个 CDN，
    // 恰恰是最容易连上之后半路断掉的那一环：接口查得到新版本，文件却下不全。
    if (!ok && !from_mirror && m_source == SRC_AUTO && !m_cancel.load())
    {
        std::remove(temp_path.c_str());
        SetStatus(ST_DOWNLOADING, "主源下载失败，改用备用源…");

        std::string mirror_error;
        if (QueryRelease(MP2_SWITCH_MIRROR_URL, true, mirror_error))
        {
            std::string mirror_url;
            bool mirror_verified = true;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                mirror_url = m_status.asset_url;
                mirror_verified = m_status.asset_verified;
                expected_size = m_status.asset_size;
                m_status.state = ST_DOWNLOADING;
                m_status.downloaded = 0;
            }
            if (!mirror_url.empty())
            {
                std::string retry_error;
                ok = m_http->DownloadToFile(mirror_url, {}, temp_path, mirror_verified,
                                            progress, retry_error);
                if (!ok)
                    error += "；备用源也失败：" + retry_error;
            }
        }
        else
        {
            error += "；备用源不可用：" + mirror_error;
        }
    }

    if (!ok)
    {
        std::remove(temp_path.c_str());
        SetStatus(ST_FAILED, "下载失败：" + error);
        return;
    }
    if (!LooksLikeNro(temp_path))
    {
        std::remove(temp_path.c_str());
        SetStatus(ST_FAILED, "下载到的文件不是有效的 NRO，已丢弃");
        return;
    }

    // 下载下来的大小必须和 Release 声明的一致。
    //
    // 这一步是在替换任何东西之前做的，因此失败时用户手上的程序完好无损。
    // 用户遇到过一次：10.9MB 的更新在卡上只剩 2MB，而下载环节报的是成功，
    // 于是流程一路走到"替换自身"才崩，把好好的程序换掉了一半。
    const uint64_t downloaded = FileUtil::GetFileSize(temp_path);
    if (expected_size > 0 && downloaded != expected_size)
    {
        std::remove(temp_path.c_str());
        SetStatus(ST_FAILED, "下载的文件大小不对（" + std::to_string(downloaded) + " / 应为 "
                             + std::to_string(expected_size) + " 字节），已放弃更新");
        return;
    }

    // 到这里新版本已经完整地躺在 temp_path 上了，但**不在运行时替换自身**。
    //
    // 实机上替换正在运行的 NRO 一直失败。原来的做法是先把自己改名成 .bak
    // 再把新文件挪过去——失败时还得回滚，而那一小段窗口里程序位置上是空的，
    // 一旦回滚也失败，用户手上就没有能启动的 NRO 了。为了一个从没成功过的操作
    // 冒这个险不值得。
    //
    // 改成下次启动时再应用（见 ApplyPendingUpdate）：那时这一份镜像早已读进内存，
    // 覆盖磁盘上的文件不影响当前进程。真在启动时也换不动的话，
    // 文件仍然留在那里，手动改个名就能用。
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.state = ST_INSTALLED;
    m_status.message = "已下载 " + m_status.latest_version + "，重启应用即可完成更新";
}

std::string CUpdater::ApplyPendingUpdate()
{
    // 调用时机很关键：必须在 romfsInit() 之前。
    // romfs 挂载的就是这个 NRO 文件本身，挂着的时候它删不掉也改不了名。
    // 这一点是照着 GBAStation 的更新器看出来的——它在替换前先 romfsExit()。
    const std::string temp_path = m_self_path + ".new";
    if (!FileUtil::Exists(temp_path))
        return std::string();

    // 半截文件、下错的东西都不能拿来替换程序
    const uint64_t size = FileUtil::GetFileSize(temp_path);
    if (!LooksLikeNro(temp_path) || size < 1024 * 1024)
    {
        std::remove(temp_path.c_str());
        return "更新文件不完整，已丢弃";
    }

    const std::string backup_path = m_self_path + ".bak";
    std::remove(backup_path.c_str());

    const bool had_old = FileUtil::Exists(m_self_path);
    if (had_old && !FileUtil::MoveOverwrite(m_self_path, backup_path))
    {
        return "更新已就绪，但换不动正在使用的文件（errno=" + std::to_string(errno)
             + "）。退出后把 " + temp_path + " 改名为 " + m_self_path + " 即可";
    }

    if (!FileUtil::MoveOverwrite(temp_path, m_self_path))
    {
        RestoreBackup(had_old, backup_path);
        return "更新已就绪，但写不进去（errno=" + std::to_string(errno)
             + "）。退出后把 " + temp_path + " 改名为 " + m_self_path + " 即可";
    }

    // 换上去之后核对大小。改名或复制都可能"成功返回"却只写了一半，
    // 而这一步写坏的是程序自己——下次就再也启动不了了。
    if (FileUtil::GetFileSize(m_self_path) != size)
    {
        RestoreBackup(had_old, backup_path);
        return "更新写入后校验不通过，已还原原有版本";
    }

    std::remove(backup_path.c_str());
    // 必须提交，否则 SD 卡上留下的是个大小为 0 的坏 NRO，下次就再也启动不了了
    FileUtil::CommitDevice(m_self_path);
    return "已应用新版本，重新启动后生效";
}
