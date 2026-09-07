#include "SoftKeyboard.h"

#include <switch.h>

#include <vector>

namespace SoftKeyboard
{

bool Show(const std::string& guide_text, const std::string& initial_text,
          std::string& out, size_t max_length)
{
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0)))
        return false;

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide_text.c_str());
    swkbdConfigSetInitialText(&kbd, initial_text.c_str());
    swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(max_length));
    swkbdConfigSetOkButtonText(&kbd, "确定");

    // swkbdShow 需要一块足够容纳 UTF-8 结果的缓冲区，按每字符最多 4 字节预留
    std::vector<char> buffer(max_length * 4 + 1, '\0');
    Result rc = swkbdShow(&kbd, buffer.data(), buffer.size());
    swkbdClose(&kbd);

    if (R_FAILED(rc))
        return false;                   // 用户取消或系统拒绝

    out.assign(buffer.data());
    return true;
}

}   // namespace SoftKeyboard
