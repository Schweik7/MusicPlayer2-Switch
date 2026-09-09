// 界面语言切换的测试。
//
// 重点不是"翻译得好不好"——那是人看的。重点是两类**只有上机才会暴露**的错：
//   1. 格式说明符对不上。中文写 %s 英文漏了，切到英文就是崩溃，不是显示问题
//   2. 底栏提示的分段数对不上。排版按 '|' 的段数均分间距，段数错了整条就乱
// 这两条能在主机上一次性扫全表，所以值得测。
#include "TestFramework.h"

#include "../source/core/Lang.h"

#include <string>

void RunLangTests()
{
    std::printf("\n--- 界面语言 ---\n");

    // 整张表的自检：格式说明符与分段数
    {
        const char* bad = Lang::ValidateTable();
        if (bad != nullptr)
            std::printf("  违规条目: %s\n", bad);
        CHECK(bad == nullptr);
    }

    // 默认是简体中文，T() 原样返回
    {
        Lang::SetLanguage(Lang::LANG_ZH);
        CHECK_EQ_INT(Lang::GetLanguage(), Lang::LANG_ZH);
        CHECK_EQ(std::string(T("设置")), "设置");
        CHECK_EQ(std::string(T("播放")), "播放");
    }

    // 切到英文
    {
        Lang::SetLanguage(Lang::LANG_EN);
        CHECK_EQ_INT(Lang::GetLanguage(), Lang::LANG_EN);
        CHECK_EQ(std::string(T("设置")), "Settings");
        CHECK_EQ(std::string(T("播放")), "Play");
        CHECK_EQ(std::string(T("暂停")), "Pause");
        CHECK_EQ(std::string(T("配色")), "Theme");
        CHECK_EQ(std::string(T("语言")), "Language");
    }

    // 表里没有的字符串退回原文，而不是空白或崩溃。
    // 这是这套做法的关键性质：漏翻一条只是那一处显示中文
    {
        Lang::SetLanguage(Lang::LANG_EN);
        CHECK_EQ(std::string(T("这条肯定不在表里的中文")), "这条肯定不在表里的中文");
        CHECK_EQ(std::string(T("")), "");
        CHECK(T(nullptr) == nullptr);
    }

    // 非中文的字符串原样穿过，不受语言影响
    {
        Lang::SetLanguage(Lang::LANG_EN);
        CHECK_EQ(std::string(T("MusicPlayer2")), "MusicPlayer2");
        Lang::SetLanguage(Lang::LANG_ZH);
        CHECK_EQ(std::string(T("MusicPlayer2")), "MusicPlayer2");
    }

    // 语言名用各自的语言写：切到看不懂的语言也认得出怎么切回来
    {
        CHECK_EQ(std::string(Lang::GetLanguageName(Lang::LANG_ZH)), "简体中文");
        CHECK_EQ(std::string(Lang::GetLanguageName(Lang::LANG_EN)), "English");
    }

    // 越界的语言值退回中文，而不是读到表外。
    // 配置文件是纯文本，用户手改成 99 是完全可能的
    {
        Lang::SetLanguage(static_cast<Lang::Language>(99));
        CHECK_EQ_INT(Lang::GetLanguage(), Lang::LANG_ZH);
        Lang::SetLanguage(static_cast<Lang::Language>(-1));
        CHECK_EQ_INT(Lang::GetLanguage(), Lang::LANG_ZH);
    }

    // 带格式说明符的条目，切换语言后仍然能安全地喂给 snprintf
    {
        Lang::SetLanguage(Lang::LANG_EN);
        char buf[128];
        std::snprintf(buf, sizeof(buf), T("音乐库扫描完成，共 %d 首"), 647);
        CHECK(std::string(buf).find("647") != std::string::npos);

        std::snprintf(buf, sizeof(buf), T("找到 %d 个结果"), 12);
        CHECK(std::string(buf).find("12") != std::string::npos);

        std::snprintf(buf, sizeof(buf), T("音量 %d%%"), 80);
        CHECK(std::string(buf).find("80%") != std::string::npos);
    }

    // 底栏提示：段数在两种语言下必须一致（上面全表扫过一遍，这里再钉住主界面那条）
    {
        const char* zh = "ZL/ZR ±5秒|右摇杆↑↓ 翻歌词|L 封面大小|R 单栏/双栏|LS 触摸|RS 下载|"
                         "B×2 退出|− 列表|＋ 设置";
        Lang::SetLanguage(Lang::LANG_ZH);
        const std::string zh_text = T(zh);
        Lang::SetLanguage(Lang::LANG_EN);
        const std::string en_text = T(zh);

        CHECK(en_text != zh_text);          // 确实翻了
        const auto count = [](const std::string& s) {
            int n = 0;
            for (char c : s)
            {
                if (c == '|')
                    ++n;
            }
            return n;
        };
        CHECK_EQ_INT(count(en_text), count(zh_text));
    }

    // 收尾：别把语言状态漏给后面的测试
    Lang::SetLanguage(Lang::LANG_ZH);
}
