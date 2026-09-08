#pragma once
#include <string>
#include <vector>

// 路径与文件操作。Switch 上路径形如 "sdmc:/music/a.mp3"，分隔符只用 '/'。
// 这里刻意不依赖 <filesystem>：devkitA64 的 libstdc++ 对它支持不完整。
namespace FileUtil
{
    struct DirEntry
    {
        std::string name;       // 仅文件名，不含目录
        bool is_dir{};
    };

    // ---- 路径 ----
    std::string GetDir(const std::string& file_path);        // 不含结尾斜杠；无目录时返回空
    std::string GetFileName(const std::string& file_path);   // 含扩展名
    std::string GetFileNameWithoutExt(const std::string& file_path);
    std::string GetExtension(const std::string& file_path, bool with_dot = false, bool upper = false);
    std::string ReplaceExtension(const std::string& file_path, const std::string& new_ext);
    std::string Combine(const std::string& dir, const std::string& name);
    // 把 '\\' 统一成 '/'，并压掉重复分隔符
    std::string NormalizeSeparators(std::string path);
    bool IsAbsolute(const std::string& path);
    // 相对路径转绝对路径，base_dir 为播放列表所在目录
    std::string RelativeToAbsolute(const std::string& path, const std::string& base_dir);

    // ---- 文件 ----
    bool Exists(const std::string& path);
    bool IsDirectory(const std::string& path);
    bool ReadAll(const std::string& path, std::string& content);
    bool WriteAll(const std::string& path, const std::string& content);
    bool CreateDirRecursive(const std::string& dir);

    // 把写入提交到存储设备。
    //
    // libnx 的原话："This should be used after each file-close where file-writing was done.
    // This is not used automatically at device unmount."
    // 不提交的话数据只停在 FS 服务的缓存里，SD 卡上的目录项元数据不会落盘——
    // 表现为文件列出来了，但大小是 0、权限全空、stat 不到。
    //
    // WriteAll / CreateDirRecursive 内部已经调过；只有直接用 FILE* 写文件的地方
    // （诊断日志、更新器下载 NRO）才需要自己调。
    // path 只用来识别设备名，非 Switch 平台是空操作。
    void CommitDevice(const std::string& path = "sdmc:/");
    // 列出目录内容，失败返回 false。结果按“目录在前、名称不区分大小写升序”排序
    bool ListDir(const std::string& dir, std::vector<DirEntry>& entries);
}
