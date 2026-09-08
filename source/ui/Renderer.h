#pragma once
#include "Theme.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
typedef struct _TTF_Font TTF_Font;

// SDL2 渲染封装。
//
// 字体来自 Switch 系统的共享字体（plGetSharedFontByType），不需要往 romfs 里塞字体文件，
// 也因此天然带中日韩字形。SDL_ttf 一个 TTF_Font 只对应一个字体文件，
// 所以这里维护一条回退链：按码点逐段挑出第一个能提供该字形的字体再分段绘制。
class CRenderer
{
public:
    enum FontSize
    {
        FS_SMALL = 0,       // 20px  次要信息
        FS_NORMAL,          // 24px  列表项
        FS_LARGE,           // 30px  标题
        FS_HUGE,            // 40px  当前曲目
        FS_LYRIC,           // 32px  歌词（非当前行）
        FS_LYRIC_CURRENT,   // 40px  歌词（当前行）
        FS_COUNT
    };

    enum Align
    {
        ALIGN_LEFT = 0,
        ALIGN_CENTER,
        ALIGN_RIGHT
    };

    CRenderer();
    ~CRenderer();

    // custom_font_path 为空或文件不存在时只用系统共享字体。
    bool Init(const std::string& custom_font_path = std::string());
    void Uninit();
    // 能否开始绘制。启动过程中用它判断进度画面画不画得出来。
    bool IsReady() const { return m_renderer != nullptr; }
    const std::string& GetLastError() const { return m_last_error; }

    void BeginFrame();
    void EndFrame();

    // ---- 基本图形 ----
    void Clear(Color color);
    void FillRect(int x, int y, int w, int h, Color color);
    void DrawRect(int x, int y, int w, int h, Color color);
    void FillRoundRect(int x, int y, int w, int h, int radius, Color color);
    // 走带按钮的图标用三角形拼出来，不依赖系统字体里是否存在 ▶ / ⏸ 这些符号
    void FillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, Color color);
    void DrawLine(int x1, int y1, int x2, int y2, Color color);
    // 只在指定矩形内绘制，用于列表裁剪
    void PushClip(int x, int y, int w, int h);
    void PopClip();

    // ---- 文本 ----
    // 返回绘制后的文本宽度
    int  DrawText(const std::string& utf8, int x, int y, FontSize size, Color color,
                  Align align = ALIGN_LEFT);
    // 超出 max_width 时以省略号截断
    int  DrawTextEllipsis(const std::string& utf8, int x, int y, int max_width, FontSize size,
                          Color color, Align align = ALIGN_LEFT);
    // 超出 max_width 时在框内来回滚动（跑马灯），否则等同 DrawText。
    // 曲目标题常常比左栏还宽，截断之后看不到后半截；滚动能让它整句读完。
    // 两端各停顿一下再折返，一直匀速跑动反而难读。
    void DrawTextMarquee(const std::string& utf8, int x, int y, int max_width, FontSize size,
                         Color color);
    void MeasureText(const std::string& utf8, FontSize size, int& width, int& height);
    // 按最大宽度折行。优先在空格处断，中日韩没有空格则允许在任意字符间断。
    // 返回的每一段都保证不超过 max_width（单个字符本身就超宽时除外）。
    std::vector<std::string> WrapText(const std::string& utf8, FontSize size, int max_width);
    int  GetLineHeight(FontSize size) const;

    // ---- 图片 ----
    // 加载专辑封面。优先用文件内嵌的封面，其次找同名图片、
    // 再其次找目录里的 cover/folder/front.jpg|png。找不到返回 nullptr。
    SDL_Texture* LoadCoverImage(const std::string& audio_file_path);
    // 直接读一个图片文件（歌词区背景用）。读不了返回 nullptr。
    SDL_Texture* LoadImageFile(const std::string& path);
    void DrawTexture(SDL_Texture* texture, int x, int y, int w, int h);
    // 带透明度的版本，用于把图片压成背景
    void DrawTexture(SDL_Texture* texture, int x, int y, int w, int h, uint8_t alpha);
    // 按"填满并居中裁切"的方式画：图片长宽比和目标区域不一致时不拉伸变形，
    // 而是等比放大到能盖住，多出来的部分裁掉。背景图必须这么画。
    void DrawTextureCover(SDL_Texture* texture, int x, int y, int w, int h, uint8_t alpha);
    void FreeTexture(SDL_Texture* texture);

    // 每帧末尾调用：淘汰长时间没用到的文字纹理
    void TrimCache();
    // 每帧开头调用：推进动画用的时间基准（跑马灯等）
    void AdvanceTime(double delta_seconds) { m_time_seconds += delta_seconds; }

    // ---- 诊断 ----
    int GetFontChainSize(FontSize size) const;
    // 返回该码点由回退链上第几个字体提供；-1 表示整条链都没有这个字形
    int FindFontIndexForCodePoint(FontSize size, char32_t code_point) const;

private:
    struct FontChain
    {
        std::vector<TTF_Font*> fonts;       // 回退链，下标 0 优先
        bool loaded{};                      // 是否已经建过；空链也算建过，不必反复重试
    };

    struct TextRun
    {
        std::string text;
        TTF_Font* font{};
    };

    struct CachedText
    {
        SDL_Texture* texture{};
        int width{};
        int height{};
        uint64_t last_used{};
    };

    // 只做 plInitialize 并把各语言字体的地址取出来缓存，不真正解析字体。
    bool LoadFonts(const std::string& custom_font_path);
    // 首次用到某个字号时才把这一档的回退链建起来。
    //
    // 六个字号 × 六个回退字体 = 36 次 TTF_OpenFontRW，全放在启动路径上要好几秒，
    // 而且期间屏幕是黑的。实际上一次会话里很多字号根本用不到。
    void EnsureChain(FontSize size) const;
    // 从 surface 建纹理并释放 surface；surface 为空时返回 nullptr
    SDL_Texture* TextureFromSurface(struct SDL_Surface* surface);
    // 把 UTF-8 串按“哪个字体能画出这个字”切成若干段
    void SplitRuns(const std::string& utf8, FontSize size, std::vector<TextRun>& runs) const;
    TTF_Font* PickFont(FontSize size, char32_t code_point) const;
    const CachedText* GetCachedText(const std::string& utf8, FontSize size, Color color);

    SDL_Window* m_window{};
    SDL_Renderer* m_renderer{};
    std::string m_last_error;
    bool m_pl_inited{};                 // 共享字体服务是否已初始化，决定要不要调 plExit

    struct SharedFont
    {
        const void* address{};
        size_t size{};
    };
    std::vector<SharedFont> m_shared_fonts;         // 字体数据，按回退顺序排列

    // 用户自带字体的字节。必须一直留着：SDL_RWFromConstMem 不复制数据，
    // 而每个字号都会各开一次 RWops 指向这块内存。
    std::string m_custom_font_data;

    mutable FontChain m_font_chains[FS_COUNT];
    std::map<std::string, CachedText> m_text_cache;
    uint64_t m_frame_counter{};
    double m_time_seconds{};

    int m_clip_depth{};
};
