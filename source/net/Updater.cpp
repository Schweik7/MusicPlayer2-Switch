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

bool CUpdater::StartCheck()
{
    if (m_busy.load() || m_http == nullptr)
        return false;
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
    m_status.from_mirror = from_mirror;
    // 只有 https 才能确认对面是谁。备用源现在走的是明文 http，
    // 那种情况下安装的是一个来路无法确认的可执行文件，得让用户知道。
    m_status.asset_verified = StringUtil::StartsWith(info.asset_url, "https://");
    return true;
}

void CUpdater::DoCheck()
{
    std::string error;
    bool ok = QueryRelease(kApiUrl, false, error);

    // GitHub 在部分地区连不上，回退到备用源。
    // 注意只在"没拿到结果"时回退：拿到了但版本更旧不算失败。
    if (!ok && !m_cancel.load())
    {
        std::string mirror_error;
        if (QueryRelease(MP2_SWITCH_MIRROR_URL, true, mirror_error))
            ok = true;
        else
            error += "；备用源也失败：" + mirror_error;
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

void CUpdater::DoInstall()
{
    std::string url;
    bool verified = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_status.asset_url;
        verified = m_status.asset_verified;
    }

    const std::string temp_path = m_self_path + ".new";
    const std::string backup_path = m_self_path + ".bak";

    std::string error;
    bool ok = m_http->DownloadToFile(
        url, {}, temp_path,
        // 会被执行的内容，能验证就一定要验。
        // 备用源目前是明文 http，验不了——换成 https 之后这里自动恢复强校验。
        verified,
        [this](uint64_t downloaded, uint64_t total) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status.downloaded = downloaded;
            if (total > 0)
                m_status.total = total;
            return !m_cancel.load();
        },
        error);

    if (!ok)
    {
        SetStatus(ST_FAILED, "下载失败：" + error);
        return;
    }
    if (!LooksLikeNro(temp_path))
    {
        std::remove(temp_path.c_str());
        SetStatus(ST_FAILED, "下载到的文件不是有效的 NRO，已丢弃");
        return;
    }

    // 替换自身。先把旧文件挪到备份名，确认新文件就位后再删备份；
    // 中途任何一步失败都要把旧文件放回去，否则程序就没了。
    //
    // 改名和复制都试：实机上 std::rename 会失败（用户遇到过"无法备份当前版本"），
    // Switch 的 FS 层对改名的支持并不可靠。复制慢一些但一定能用。
    std::remove(backup_path.c_str());
    bool had_old = FileUtil::Exists(m_self_path);
    if (had_old && !FileUtil::MoveOverwrite(m_self_path, backup_path))
    {
        std::remove(temp_path.c_str());
        SetStatus(ST_FAILED, "无法备份当前版本，已放弃更新（errno=" + std::to_string(errno) + "）");
        return;
    }
    if (!FileUtil::MoveOverwrite(temp_path, m_self_path))
    {
        if (had_old)
            FileUtil::MoveOverwrite(backup_path, m_self_path);       // 回滚
        std::remove(temp_path.c_str());
        SetStatus(ST_FAILED, "无法写入新版本，已还原原有版本（errno="
                             + std::to_string(errno) + "）");
        return;
    }
    std::remove(backup_path.c_str());
    // 整个替换过程都要提交，否则 SD 卡上留下的是个大小为 0 的坏 NRO，
    // 下次就再也启动不了了
    FileUtil::CommitDevice(m_self_path);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.state = ST_INSTALLED;
    m_status.message = "已更新到 " + m_status.latest_version + "，请退出后重新启动";
}
