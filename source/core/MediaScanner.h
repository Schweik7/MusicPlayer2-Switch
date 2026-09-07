#pragma once
#include "SongInfo.h"
#include <atomic>
#include <string>
#include <vector>

// 扫描 SD 卡上的音频文件。Switch 的文件系统访问较慢，扫描放在后台线程里做，
// 通过 GetProgress()/TakeResult() 与 UI 线程通信。
class CMediaScanner
{
public:
    struct Progress
    {
        int scanned_dirs{};
        int found_files{};
        bool running{};
        bool finished{};
    };

    // Switch 端 SDL2_mixer 能解码的格式；与桌面版支持的格式取交集
    static bool IsSupportedAudio(const std::string& file_path);
    static const std::vector<std::string>& GetSupportedExtensions();

    // 同步扫描（单元测试与小目录用）。max_depth 为负表示不限制。
    static void ScanDirectory(const std::string& dir, std::vector<SongInfo>& result,
                              int max_depth = -1, const std::atomic<bool>* cancel = nullptr);

    // 从文件名推断标题与艺术家：优先 "艺术家 - 标题"，否则整体作为标题
    static void FillTagFromFileName(SongInfo& song);
};
