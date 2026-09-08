#pragma once
#include <string>

class CPlayer;
class CRenderer;
class CInputMap;

enum ScreenId
{
    SCREEN_PLAYER = 0,      // 播放界面（歌词 / 频谱）
    SCREEN_PLAYLIST,        // 当前播放列表
    SCREEN_BROWSER,         // SD 卡文件浏览
    SCREEN_DOWNLOAD,        // 在线歌词/封面下载
    SCREEN_SETTINGS,        // 设置 / 关于 / 检查更新
    SCREEN_COUNT,
    SCREEN_NONE = -1
};

// 界面之间共享的上下文
struct ScreenContext
{
    CPlayer* player{};
    CRenderer* renderer{};
    CInputMap* input{};

    // 由界面填写，App 在本帧末尾处理
    ScreenId next_screen{ SCREEN_NONE };
    bool request_exit{};

    // 一闪而过的提示条，由 App 负责绘制和倒计时
    std::string toast_text;
    double toast_timer{};

    void ShowToast(const std::string& message)
    {
        toast_text = message;
        toast_timer = 2.0;
    }
};

class CScreen
{
public:
    virtual ~CScreen() = default;

    // 进入该界面时调用一次
    virtual void OnEnter(ScreenContext& ctx) { (void)ctx; }
    virtual void OnLeave(ScreenContext& ctx) { (void)ctx; }

    // App 在销毁 CRenderer 之前显式调用，用于释放界面持有的纹理。
    // 不能放在析构函数里：那时 CRenderer 可能已经先一步销毁了。
    virtual void ReleaseResources(ScreenContext& ctx) { (void)ctx; }

    virtual void Update(ScreenContext& ctx, double delta_seconds) = 0;
    virtual void Draw(ScreenContext& ctx) = 0;

    // 返回上一层。B 键和顶栏的返回按钮都走这里——
    // "B 键做什么"只写一次，两条路径就不会走偏。
    virtual void GoBack(ScreenContext& ctx) { ctx.next_screen = SCREEN_PLAYER; }
    // 顶栏要不要画返回按钮。播放界面是根，没有上一层。
    virtual bool CanGoBack() const { return true; }

    // 该界面在顶栏显示的名字
    virtual const char* GetTitle() const = 0;
    // 底栏的按键提示
    virtual const char* GetButtonHints() const = 0;
};
