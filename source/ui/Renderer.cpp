#include "Renderer.h"
#include "../Diagnostics.h"
#include "../core/AudioTag.h"
#include "../core/FileUtil.h"
#include "../core/StringUtil.h"

#include <switch.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    // 各档字号的像素高度
    const int kFontPixelSize[CRenderer::FS_COUNT] = { 20, 24, 30, 40, 32, 40 };

    // 文字纹理缓存里超过这么多帧没被用到就释放
    const uint64_t kCacheTtlFrames = 240;
    const size_t kCacheMaxEntries = 512;

    SDL_Color ToSdlColor(Color color)
    {
        SDL_Color result;
        result.r = color.r;
        result.g = color.g;
        result.b = color.b;
        result.a = color.a;
        return result;
    }
}

CRenderer::CRenderer()
{
}

CRenderer::~CRenderer()
{
    Uninit();
}

bool CRenderer::Init(const std::string& custom_font_path)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        m_last_error = std::string("SDL_Init 失败: ") + SDL_GetError();
        return false;
    }

    // Switch 上窗口尺寸由系统决定（掌机 1280x720 / 底座 1920x1080），
    // 用逻辑分辨率把两种情况统一到 1280x720 的坐标系里
    m_window = SDL_CreateWindow("MusicPlayer2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                Theme::kScreenWidth, Theme::kScreenHeight, 0);
    if (m_window == nullptr)
    {
        m_last_error = std::string("SDL_CreateWindow 失败: ") + SDL_GetError();
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (m_renderer == nullptr)
    {
        m_last_error = std::string("SDL_CreateRenderer 失败: ") + SDL_GetError();
        return false;
    }
    SDL_RenderSetLogicalSize(m_renderer, Theme::kScreenWidth, Theme::kScreenHeight);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    if (TTF_Init() != 0)
    {
        m_last_error = std::string("TTF_Init 失败: ") + TTF_GetError();
        return false;
    }
    // 封面是可选功能，初始化失败不算致命错误
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);

    if (!LoadFonts(custom_font_path))
        return false;

    return true;
}

bool CRenderer::LoadFonts(const std::string& custom_font_path)
{
    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc))
    {
        m_last_error = "plInitialize 失败，无法取得系统共享字体";
        return false;
    }
    m_pl_inited = true;

    // 回退链的顺序：先标准拉丁/日文，再简体中文，再扩展汉字、繁体、韩文，最后是任天堂扩展符号。
    // 系统共享字体在所有机器上都存在，因此不需要往 romfs 里打包字体文件。
    const PlSharedFontType kFontTypes[] = {
        PlSharedFontType_Standard,
        PlSharedFontType_ChineseSimplified,
        PlSharedFontType_ExtChineseSimplified,
        PlSharedFontType_ChineseTraditional,
        PlSharedFontType_KO,
        PlSharedFontType_NintendoExt,
    };

    // 用户自带字体排在最前面，系统字体留在后面兜底：
    // 自带字体缺哪个字形，PickFont 会自动往后找，不会出现豆腐块。
    if (!custom_font_path.empty() && FileUtil::ReadAll(custom_font_path, m_custom_font_data)
        && m_custom_font_data.size() > 1024)
    {
        m_shared_fonts.push_back(SharedFont{ m_custom_font_data.data(),
                                             m_custom_font_data.size() });
    }
    else
    {
        m_custom_font_data.clear();
    }

    // 这里只把字体数据的地址取出来。真正的 TTF_OpenFontRW 交给 EnsureChain，
    // 用到哪一档才解析哪一档——36 次解析全压在启动路径上要好几秒的黑屏。
    for (PlSharedFontType type : kFontTypes)
    {
        PlFontData font_data{};
        if (R_FAILED(plGetSharedFontByType(&font_data, type)))
            continue;                                   // 某些语言的字体在部分机型上可能缺失
        m_shared_fonts.push_back(SharedFont{ font_data.address,
                                             static_cast<size_t>(font_data.size) });
    }

    if (m_shared_fonts.empty())
    {
        m_last_error = "没有可用的系统字体";
        return false;
    }
    return true;
}

void CRenderer::EnsureChain(FontSize size) const
{
    FontChain& chain = m_font_chains[size];
    if (chain.loaded)
        return;
    chain.loaded = true;

    for (const SharedFont& shared : m_shared_fonts)
    {
        SDL_RWops* rw = SDL_RWFromConstMem(shared.address, static_cast<int>(shared.size));
        if (rw == nullptr)
            continue;
        // freesrc = 1：TTF_CloseFont 时一并释放 RWops，字体数据本身归系统所有
        TTF_Font* font = TTF_OpenFontRW(rw, 1, kFontPixelSize[size]);
        if (font != nullptr)
            chain.fonts.push_back(font);
    }
}

void CRenderer::Uninit()
{
    for (auto& pair : m_text_cache)
    {
        if (pair.second.texture != nullptr)
            SDL_DestroyTexture(pair.second.texture);
    }
    m_text_cache.clear();

    for (FontChain& chain : m_font_chains)
    {
        for (TTF_Font* font : chain.fonts)
            TTF_CloseFont(font);
        chain.fonts.clear();
    }

    if (m_pl_inited)
    {
        plExit();
        m_pl_inited = false;
    }

    if (TTF_WasInit())
        TTF_Quit();
    IMG_Quit();

    if (m_renderer != nullptr)
    {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window != nullptr)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

void CRenderer::BeginFrame()
{
    ++m_frame_counter;
}

void CRenderer::EndFrame()
{
    SDL_RenderPresent(m_renderer);
}

void CRenderer::Clear(Color color)
{
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(m_renderer);
}

void CRenderer::FillRect(int x, int y, int w, int h, Color color)
{
    if (w <= 0 || h <= 0)
        return;
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_Rect rect{ x, y, w, h };
    SDL_RenderFillRect(m_renderer, &rect);
}

void CRenderer::DrawRect(int x, int y, int w, int h, Color color)
{
    if (w <= 0 || h <= 0)
        return;
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_Rect rect{ x, y, w, h };
    SDL_RenderDrawRect(m_renderer, &rect);
}

void CRenderer::FillRoundRect(int x, int y, int w, int h, int radius, Color color)
{
    if (w <= 0 || h <= 0)
        return;
    radius = std::min(radius, std::min(w, h) / 2);
    if (radius <= 0)
    {
        FillRect(x, y, w, h, color);
        return;
    }

    // 中间的十字部分用矩形填充，四角逐行算出圆弧宽度
    FillRect(x + radius, y, w - 2 * radius, h, color);
    FillRect(x, y + radius, radius, h - 2 * radius, color);
    FillRect(x + w - radius, y + radius, radius, h - 2 * radius, color);

    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    for (int dy = 0; dy < radius; ++dy)
    {
        // 圆心在 (radius, radius)，当前行距圆心 radius-dy
        int offset = radius - static_cast<int>(
            SDL_sqrt(static_cast<double>(radius * radius - (radius - dy) * (radius - dy))));
        int line_x = x + offset;
        int line_w = w - 2 * offset;
        SDL_Rect top{ line_x, y + dy, line_w, 1 };
        SDL_Rect bottom{ line_x, y + h - 1 - dy, line_w, 1 };
        SDL_RenderFillRect(m_renderer, &top);
        SDL_RenderFillRect(m_renderer, &bottom);
    }
}

void CRenderer::FillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, Color color)
{
    SDL_Color c = ToSdlColor(color);
    SDL_Vertex verts[3];
    verts[0].position = SDL_FPoint{ static_cast<float>(x1), static_cast<float>(y1) };
    verts[1].position = SDL_FPoint{ static_cast<float>(x2), static_cast<float>(y2) };
    verts[2].position = SDL_FPoint{ static_cast<float>(x3), static_cast<float>(y3) };
    for (SDL_Vertex& v : verts)
    {
        v.color = c;
        v.tex_coord = SDL_FPoint{ 0.0f, 0.0f };
    }
    SDL_RenderGeometry(m_renderer, nullptr, verts, 3, nullptr, 0);
}

void CRenderer::DrawLine(int x1, int y1, int x2, int y2, Color color)
{
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(m_renderer, x1, y1, x2, y2);
}

void CRenderer::PushClip(int x, int y, int w, int h)
{
    SDL_Rect rect{ x, y, w, h };
    SDL_RenderSetClipRect(m_renderer, &rect);
    ++m_clip_depth;
}

void CRenderer::PopClip()
{
    if (m_clip_depth > 0)
        --m_clip_depth;
    if (m_clip_depth == 0)
        SDL_RenderSetClipRect(m_renderer, nullptr);
}

TTF_Font* CRenderer::PickFont(FontSize size, char32_t code_point) const
{
    EnsureChain(size);
    const FontChain& chain = m_font_chains[size];
    for (TTF_Font* font : chain.fonts)
    {
// TTF_GlyphIsProvided32 是 SDL_ttf 2.0.18 才有的；老版本只能查 BMP 范围内的码点
#if defined(SDL_TTF_VERSION_ATLEAST) && SDL_TTF_VERSION_ATLEAST(2, 0, 18)
        if (TTF_GlyphIsProvided32(font, static_cast<Uint32>(code_point)))
            return font;
#else
        if (code_point <= 0xFFFF && TTF_GlyphIsProvided(font, static_cast<Uint16>(code_point)))
            return font;
#endif
    }
    // 都没有就用第一个字体画出“豆腐块”，至少能看出这里有字
    return chain.fonts.front();
}

std::vector<std::string> CRenderer::WrapText(const std::string& utf8, FontSize size,
                                             int max_width)
{
    std::vector<std::string> result;
    if (utf8.empty() || max_width <= 0)
    {
        result.push_back(utf8);
        return result;
    }

    int total_w = 0, total_h = 0;
    MeasureText(utf8, size, total_w, total_h);
    if (total_w <= max_width)
    {
        result.push_back(utf8);
        return result;
    }

    std::string line;
    // last_break 记录当前行里最后一个可断点（空格之后）的字节位置。
    // 中日韩没有空格，找不到可断点时就在当前字符处硬断——总比截断成省略号强。
    size_t last_break = std::string::npos;

    size_t i = 0;
    while (i < utf8.size())
    {
        size_t len = StringUtil::Utf8CharLen(utf8, i);
        std::string ch = utf8.substr(i, len);
        i += len;

        std::string candidate = line + ch;
        int w = 0, h = 0;
        MeasureText(candidate, size, w, h);

        if (w > max_width && !line.empty())
        {
            if (last_break != std::string::npos && last_break > 0)
            {
                // 回退到最近的空格处断行，把剩下的字放回下一行
                result.push_back(StringUtil::Trimmed(line.substr(0, last_break)));
                line = line.substr(last_break) + ch;
            }
            else
            {
                result.push_back(line);
                line = ch;
            }
            last_break = std::string::npos;
        }
        else
        {
            line = candidate;
        }

        if (ch == " ")
            last_break = line.size();
    }

    if (!line.empty())
        result.push_back(StringUtil::Trimmed(line));
    if (result.empty())
        result.push_back(utf8);
    return result;
}

int CRenderer::GetFontChainSize(FontSize size) const
{
    EnsureChain(size);
    return static_cast<int>(m_font_chains[size].fonts.size());
}

int CRenderer::FindFontIndexForCodePoint(FontSize size, char32_t code_point) const
{
    // 和 PickFont 的查找逻辑一致，区别是找不到时返回 -1 而不是退回第一个字体，
    // 这样诊断输出才能区分“真的有这个字形”和“画成了豆腐块”。
    EnsureChain(size);
    const FontChain& chain = m_font_chains[size];
    for (size_t i = 0; i < chain.fonts.size(); ++i)
    {
#if defined(SDL_TTF_VERSION_ATLEAST) && SDL_TTF_VERSION_ATLEAST(2, 0, 18)
        if (TTF_GlyphIsProvided32(chain.fonts[i], static_cast<Uint32>(code_point)))
            return static_cast<int>(i);
#else
        if (code_point <= 0xFFFF
            && TTF_GlyphIsProvided(chain.fonts[i], static_cast<Uint16>(code_point)))
            return static_cast<int>(i);
#endif
    }
    return -1;
}

void CRenderer::SplitRuns(const std::string& utf8, FontSize size, std::vector<TextRun>& runs) const
{
    runs.clear();
    size_t i = 0;
    while (i < utf8.size())
    {
        size_t len = StringUtil::Utf8CharLen(utf8, i);
        std::string ch = utf8.substr(i, len);
        std::vector<char32_t> cps = StringUtil::Utf8ToCodePoints(ch);
        TTF_Font* font = PickFont(size, cps.empty() ? U'?' : cps[0]);

        if (!runs.empty() && runs.back().font == font)
            runs.back().text += ch;
        else
        {
            TextRun run;
            run.text = ch;
            run.font = font;
            runs.push_back(run);
        }
        i += len;
    }
}

const CRenderer::CachedText* CRenderer::GetCachedText(const std::string& utf8, FontSize size, Color color)
{
    if (utf8.empty())
        return nullptr;

    char key_suffix[48];
    std::snprintf(key_suffix, sizeof(key_suffix), "\x1F%d\x1F%02X%02X%02X%02X",
                  static_cast<int>(size), color.r, color.g, color.b, color.a);
    std::string key = utf8 + key_suffix;

    auto iter = m_text_cache.find(key);
    if (iter != m_text_cache.end())
    {
        iter->second.last_used = m_frame_counter;
        return &iter->second;
    }

    std::vector<TextRun> runs;
    SplitRuns(utf8, size, runs);
    if (runs.empty())
        return nullptr;

    // 先把每一段渲染成 surface，再拼成一张，这样整串文字只占一个纹理
    std::vector<SDL_Surface*> surfaces;
    int total_width = 0;
    int max_height = 0;
    SDL_Color sdl_color = ToSdlColor(color);
    for (const TextRun& run : runs)
    {
        SDL_Surface* surface = TTF_RenderUTF8_Blended(run.font, run.text.c_str(), sdl_color);
        if (surface == nullptr)
            continue;
        surfaces.push_back(surface);
        total_width += surface->w;
        max_height = std::max(max_height, surface->h);
    }
    if (surfaces.empty())
        return nullptr;

    SDL_Surface* combined = SDL_CreateRGBSurfaceWithFormat(0, total_width, max_height, 32,
                                                           SDL_PIXELFORMAT_RGBA32);
    if (combined == nullptr)
    {
        for (SDL_Surface* s : surfaces)
            SDL_FreeSurface(s);
        return nullptr;
    }

    int offset_x = 0;
    for (SDL_Surface* s : surfaces)
    {
        // 各段字体的基线可能不同，按底部对齐能让混排的中英文看起来在一条线上
        SDL_Rect dst{ offset_x, max_height - s->h, s->w, s->h };
        SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(s, nullptr, combined, &dst);
        offset_x += s->w;
        SDL_FreeSurface(s);
    }

    CachedText entry;
    entry.texture = SDL_CreateTextureFromSurface(m_renderer, combined);
    entry.width = combined->w;
    entry.height = combined->h;
    entry.last_used = m_frame_counter;
    SDL_FreeSurface(combined);

    if (entry.texture == nullptr)
        return nullptr;
    SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);

    auto result = m_text_cache.emplace(key, entry);
    return &result.first->second;
}

int CRenderer::DrawText(const std::string& utf8, int x, int y, FontSize size, Color color, Align align)
{
    const CachedText* cached = GetCachedText(utf8, size, color);
    if (cached == nullptr)
        return 0;

    int draw_x = x;
    if (align == ALIGN_CENTER)
        draw_x = x - cached->width / 2;
    else if (align == ALIGN_RIGHT)
        draw_x = x - cached->width;

    SDL_Rect dst{ draw_x, y, cached->width, cached->height };
    SDL_RenderCopy(m_renderer, cached->texture, nullptr, &dst);
    return cached->width;
}

void CRenderer::DrawTextMarquee(const std::string& utf8, int x, int y, int max_width,
                                FontSize size, Color color)
{
    int width = 0, height = 0;
    MeasureText(utf8, size, width, height);
    if (width <= max_width)
    {
        DrawText(utf8, x, y, size, color);
        return;
    }

    // 一个来回：停顿 → 向左跑完超出的部分 → 停顿 → 原路退回
    const double travel = width - max_width;
    const double kSpeed = 45.0;             // 像素/秒
    const double kHold = 1.6;               // 两端的停顿时长（秒）
    const double half = travel / kSpeed + kHold;
    const double t = std::fmod(m_time_seconds, half * 2.0);

    double offset;
    if (t < kHold)
        offset = 0.0;
    else if (t < half)
        offset = (t - kHold) * kSpeed;
    else if (t < half + kHold)
        offset = travel;
    else
        offset = travel - (t - half - kHold) * kSpeed;

    PushClip(x, y, max_width, height);
    DrawText(utf8, x - static_cast<int>(offset), y, size, color);
    PopClip();
}

int CRenderer::DrawTextEllipsis(const std::string& utf8, int x, int y, int max_width, FontSize size,
                                Color color, Align align)
{
    int width = 0, height = 0;
    MeasureText(utf8, size, width, height);
    if (width <= max_width)
        return DrawText(utf8, x, y, size, color, align);

    // 二分找出能放下的最多字符数，再补上省略号
    const std::string ellipsis = "...";
    int ellipsis_w = 0, ellipsis_h = 0;
    MeasureText(ellipsis, size, ellipsis_w, ellipsis_h);
    int budget = max_width - ellipsis_w;
    if (budget <= 0)
        return DrawText(ellipsis, x, y, size, color, align);

    size_t total_chars = StringUtil::Utf8Length(utf8);
    size_t lo = 0, hi = total_chars;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo + 1) / 2;
        int w = 0, h = 0;
        MeasureText(StringUtil::Utf8Substr(utf8, mid), size, w, h);
        if (w <= budget)
            lo = mid;
        else
            hi = mid - 1;
    }
    return DrawText(StringUtil::Utf8Substr(utf8, lo) + ellipsis, x, y, size, color, align);
}

void CRenderer::MeasureText(const std::string& utf8, FontSize size, int& width, int& height)
{
    width = 0;
    height = GetLineHeight(size);
    if (utf8.empty())
        return;

    std::vector<TextRun> runs;
    SplitRuns(utf8, size, runs);
    for (const TextRun& run : runs)
    {
        int w = 0, h = 0;
        if (TTF_SizeUTF8(run.font, run.text.c_str(), &w, &h) == 0)
        {
            width += w;
            height = std::max(height, h);
        }
    }
}

int CRenderer::GetLineHeight(FontSize size) const
{
    EnsureChain(size);
    const FontChain& chain = m_font_chains[size];
    if (chain.fonts.empty())
        return kFontPixelSize[size];
    return TTF_FontHeight(chain.fonts.front());
}

SDL_Texture* CRenderer::TextureFromSurface(SDL_Surface* surface)
{
    if (surface == nullptr)
        return nullptr;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

SDL_Texture* CRenderer::LoadCoverImage(const std::string& audio_file_path)
{
    // ---- 先试文件内嵌的封面 ----
    //
    // 这条路比找旁边的图片文件可靠：图片一定跟这首歌对得上，也不依赖
    // 下载工具有没有把封面单独导出来。Switch 上文件名只能是 ASCII，
    // 靠文件名配对本来就容易出岔子。
    AudioTag::Picture picture;
    if (AudioTag::ReadCover(audio_file_path, picture) && !picture.data.empty())
    {
        SDL_RWops* rw = SDL_RWFromConstMem(picture.data.data(),
                                           static_cast<int>(picture.data.size()));
        if (rw != nullptr)
        {
            // freesrc = 1：由 SDL 负责释放 RWops，图片字节仍归 picture 所有
            SDL_Surface* surface = IMG_Load_RW(rw, 1);
            if (surface != nullptr)
            {
                SDL_Texture* texture = TextureFromSurface(surface);
                if (texture != nullptr)
                    return texture;
            }
            else
            {
                Diag::Logf("内嵌封面解码失败 (%s, %u 字节): %s", picture.mime.c_str(),
                           static_cast<unsigned>(picture.data.size()), IMG_GetError());
            }
        }
    }

    // ---- 退回目录里的图片文件 ----
    std::string dir = FileUtil::GetDir(audio_file_path);
    if (dir.empty())
        return nullptr;

    // 先找与音频同名的图片，再找目录里的通用封面名
    const char* image_exts[] = { ".jpg", ".jpeg", ".png" };
    std::vector<std::string> candidates;
    for (const char* ext : image_exts)
        candidates.push_back(FileUtil::ReplaceExtension(audio_file_path, ext));
    const char* cover_names[] = { "cover", "folder", "front", "album", "Cover", "Folder" };
    for (const char* name : cover_names)
    {
        for (const char* ext : image_exts)
            candidates.push_back(FileUtil::Combine(dir, std::string(name) + ext));
    }

    for (const std::string& path : candidates)
    {
        if (!FileUtil::Exists(path))
            continue;
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (surface == nullptr)
        {
            // 记下来：文件明明在，却解码失败，这种情况必须能查
            Diag::Logf("封面文件解码失败 %s: %s", path.c_str(), IMG_GetError());
            continue;
        }
        SDL_Texture* texture = TextureFromSurface(surface);
        if (texture != nullptr)
            return texture;
        Diag::Logf("封面纹理创建失败 %s: %s", path.c_str(), SDL_GetError());
    }

    Diag::Logf("没有找到可用封面: %s", audio_file_path.c_str());
    return nullptr;
}

SDL_Texture* CRenderer::LoadImageFile(const std::string& path)
{
    if (path.empty() || !FileUtil::Exists(path))
        return nullptr;
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (surface == nullptr)
        return nullptr;
    return TextureFromSurface(surface);
}

void CRenderer::DrawTexture(SDL_Texture* texture, int x, int y, int w, int h)
{
    if (texture == nullptr)
        return;
    SDL_Rect dst{ x, y, w, h };
    SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
}

void CRenderer::DrawTexture(SDL_Texture* texture, int x, int y, int w, int h, uint8_t alpha)
{
    if (texture == nullptr)
        return;
    SDL_SetTextureAlphaMod(texture, alpha);
    SDL_Rect dst{ x, y, w, h };
    SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
    SDL_SetTextureAlphaMod(texture, 255);       // 纹理是共用的，用完要还原
}

void CRenderer::DrawTextureCover(SDL_Texture* texture, int x, int y, int w, int h, uint8_t alpha)
{
    if (texture == nullptr || w <= 0 || h <= 0)
        return;

    int tex_w = 0, tex_h = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) != 0
        || tex_w <= 0 || tex_h <= 0)
    {
        DrawTexture(texture, x, y, w, h, alpha);
        return;
    }

    // 从原图里裁一块和目标区域同长宽比的最大矩形，居中取
    const double target_ratio = static_cast<double>(w) / h;
    const double source_ratio = static_cast<double>(tex_w) / tex_h;
    SDL_Rect src{ 0, 0, tex_w, tex_h };
    if (source_ratio > target_ratio)
    {
        src.w = static_cast<int>(tex_h * target_ratio);
        src.x = (tex_w - src.w) / 2;
    }
    else
    {
        src.h = static_cast<int>(tex_w / target_ratio);
        src.y = (tex_h - src.h) / 2;
    }

    SDL_SetTextureAlphaMod(texture, alpha);
    SDL_Rect dst{ x, y, w, h };
    SDL_RenderCopy(m_renderer, texture, &src, &dst);
    SDL_SetTextureAlphaMod(texture, 255);
}

void CRenderer::FreeTexture(SDL_Texture* texture)
{
    if (texture != nullptr)
        SDL_DestroyTexture(texture);
}

void CRenderer::TrimCache()
{
    if (m_text_cache.size() < kCacheMaxEntries)
    {
        // 未超上限时只淘汰明显过期的条目，避免每帧遍历造成抖动
        if (m_frame_counter % 60 != 0)
            return;
    }

    for (auto iter = m_text_cache.begin(); iter != m_text_cache.end(); )
    {
        if (m_frame_counter - iter->second.last_used > kCacheTtlFrames)
        {
            SDL_DestroyTexture(iter->second.texture);
            iter = m_text_cache.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}
