#pragma once
#include "../input/InputMap.h"

// 列表的手指拖动滚动（带松手惯性）。
//
// 播放列表和文件浏览两个界面都需要，逻辑本身有边界钳制和惯性衰减，
// 复制两份迟早会改歪一份，所以抽出来共用。
// 滚动状态仍由各界面自己持有——它们的 Draw 直接用这几个变量。
namespace ListScroller
{
    struct Params
    {
        int list_top;           // 列表首行的屏幕 y 坐标
        int item_height;
        int count;              // 总条目数
        int visible;            // 一屏能显示几条
    };

    // 返回 true 表示本帧的滚动位置由触摸决定。
    // 此时调用方不应再执行"让选中项可见"那类会把列表拽回去的逻辑。
    bool Update(const CInputMap::TouchState& touch, const Params& params, double delta_seconds,
                int& scroll, double& scroll_smooth, bool& dragging, double& fling);
}
