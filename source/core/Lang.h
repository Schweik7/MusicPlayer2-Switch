#pragma once

// 界面语言。默认简体中文，可在设置里切到英文。
//
// 取的是 gettext 那套做法：**用中文原文本身当键**，而不是另起一套 STR_XXX 的符号。
// 这样做的好处在这个项目里特别明显：
//   - 改动最小。原来写 "设置" 的地方改成 T("设置") 就完了，264 处都是机械替换
//   - 中文始终是唯一事实来源，代码读起来还是中文，不用回头查符号表
//   - 缺翻译时自动退回中文。漏一条只是那一处显示中文，不会变成空白或 "STR_1234"
//
// 代价是改中文原文等于换了键，对应的翻译会失效退回中文。所以改文案时
// 要顺手改表里的键——这一点写在 DEVELOPMENT 里了。
namespace Lang
{
    enum Language
    {
        LANG_ZH = 0,        // 简体中文
        LANG_EN = 1,        // English
        LANG_COUNT
    };

    void SetLanguage(Language lang);
    Language GetLanguage();

    // 语言的显示名（用它自己的语言写，切换时才认得出来）
    const char* GetLanguageName(Language lang);

    // 查表。查不到就原样返回传进来的指针——调用处传的都是字符串字面量，
    // 静态存储期，返回它安全；命中时返回的是表里那份，同样是静态的。
    // 两条路都不产生临时对象，所以每帧调几十次也没有分配开销。
    const char* Translate(const char* zh);

    // 表的自检。两条不变量，错了都是上机之后才看得出来的那种错：
    //   1. printf 的格式说明符必须逐个对应——漏一个 %s 就是崩溃，不是显示问题
    //   2. 底栏提示按 '|' 分段均分间距，段数对不上排版就乱
    // 返回第一条违规的中文原文；全部通过返回 nullptr。
    const char* ValidateTable();
}

// 放在全局命名空间：调用处写 T("…") 就行，不必到处写 Lang::Translate
inline const char* T(const char* zh)
{
    return Lang::Translate(zh);
}
