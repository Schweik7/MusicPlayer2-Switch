#pragma once
#include <string>

// Switch 系统软键盘的封装（libnx swkbd）。
// 调用期间系统会接管画面，返回后再继续渲染，因此必须在帧与帧之间调用。
namespace SoftKeyboard
{
    // 弹出键盘让用户输入文本。用户取消时返回 false，out 不变。
    bool Show(const std::string& guide_text, const std::string& initial_text,
              std::string& out, size_t max_length = 128);
}
