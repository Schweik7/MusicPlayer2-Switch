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

    bool Init();
    void Uninit();
    const std::string& GetLastError() const { return m_last_error; }

    void BeginFrame();
    void EndFrame();

    // ---- 基本图形 ----
    void Clear(Color color);
    void FillRect(int x, int y, int w, int h, Color color);
    void DrawRect(int x, int y, int w, int h, Color color);
    void FillRoundRect(int x, int y, int w, int h, int radius, Color color);
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
    void MeasureText(const std::string& utf8, FontSize size, int& width, int& height);
    int  GetLineHeight(FontSize size) const;

    // ---- 图片 ----
    // 加载专辑封面（歌曲同目录下的 cover/folder/front.jpg|png）。找不到返回 nullptr。
    SDL_Texture* LoadCoverImage(const std::string& audio_file_path);
    void DrawTexture(SDL_Texture* texture, int x, int y, int w, int h);
    void FreeTexture(SDL_Texture* texture);

    // 每帧末尾调用：淘汰长时间没用到的文字纹理
    void TrimCache();

private:
    struct FontChain
    {
        std::vector<TTF_Font*> fonts;       // 回退链，下标 0 优先
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

    bool LoadFonts();
    // 把 UTF-8 串按“哪个字体能画出这个字”切成若干段
    void SplitRuns(const std::string& utf8, FontSize size, std::vector<TextRun>& runs) const;
    TTF_Font* PickFont(FontSize size, char32_t code_point) const;
    const CachedText* GetCachedText(const std::string& utf8, FontSize size, Color color);

    SDL_Window* m_window{};
    SDL_Renderer* m_renderer{};
    std::string m_last_error;
    bool m_pl_inited{};                 // 共享字体服务是否已初始化，决定要不要调 plExit

    FontChain m_font_chains[FS_COUNT];
    std::map<std::string, CachedText> m_text_cache;
    uint64_t m_frame_counter{};

    int m_clip_depth{};
};
