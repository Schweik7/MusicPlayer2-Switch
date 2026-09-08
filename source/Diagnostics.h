#pragma once
#include <string>

class CRenderer;
class CCurlHttpClient;

// 实机诊断。
//
// 通过 nxlink 启动时（nxlink -s ...）自动启用，把探测结果打到开发机的终端上；
// 也可以在 SD 卡上放一个 sdmc:/switch/MusicPlayer2/diag.flag 手动打开。
// 正常从 hbmenu 启动时全部是空操作，不影响启动速度。
namespace Diag
{
    // 在 main 最开头调用。返回是否启用了诊断输出。
    bool Begin();
    void End();
    bool IsEnabled();

    void Logf(const char* format, ...);
    // 打印一段字节的十六进制，用来确认字符串在传输/存储过程中有没有被改写
    void LogBytes(const char* label, const std::string& data);

    // 探测 SD 卡对非 ASCII 文件名的支持情况。
    // 按 UTF-8 编码长度分组测试（2/3/4 字节），以便区分"完全不支持非 ASCII"
    // 和"只是某类字符不行"。
    void ProbeFileSystem();

    // 探测系统共享字体对中日韩字形的覆盖情况
    void ProbeFont(const CRenderer& renderer);

    // 逐条枚举一个真实目录并 stat，报告程序实际能看到/能打开多少条目。
    // 这是判断"中文文件到底能不能用"最直接的证据。
    void ProbeDirectory(const std::string& dir);

    // 网络与 TLS 自检：报告 CA 证书包的位置和大小，并真的发一次
    // 更新检查会用到的那个请求，把 curl 的错误码原样记下来。
    void ProbeNetwork(CCurlHttpClient& http);
}
