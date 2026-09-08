#include "ListScroller.h"

#include <algorithm>
#include <cmath>

namespace ListScroller
{

bool Update(const CInputMap::TouchState& touch, const Params& params, double delta_seconds,
            int& scroll, double& scroll_smooth, bool& dragging, double& fling)
{
    const int list_bottom = params.list_top + params.visible * params.item_height;
    const double max_scroll = std::max(0, params.count - params.visible);

    if (touch.pressed && touch.y >= params.list_top && touch.y < list_bottom)
    {
        dragging = true;
        fling = 0.0;
    }

    if (dragging)
    {
        if (touch.touching)
        {
            double rows = static_cast<double>(touch.step_y) / params.item_height;
            scroll_smooth -= rows;
            // 记下松手瞬间的速度作为惯性初值。
            // delta 设下限：掉帧那一帧会除出一个离谱的速度，甩出去就收不回来了
            if (delta_seconds > 1e-4)
                fling = -rows / delta_seconds;
        }
        else
        {
            dragging = false;
        }
    }
    else if (std::abs(fling) > 0.05)
    {
        scroll_smooth += fling * delta_seconds;
        fling *= std::pow(0.05, delta_seconds);         // 每秒衰减到 5%
    }
    else
    {
        fling = 0.0;
        return false;
    }

    // 撞到两端就把惯性清掉，否则会一直顶着边界空转
    if (scroll_smooth < 0.0 || scroll_smooth > max_scroll)
        fling = 0.0;
    scroll_smooth = std::max(0.0, std::min(scroll_smooth, max_scroll));
    scroll = static_cast<int>(scroll_smooth + 0.5);
    return true;
}

}   // namespace ListScroller
