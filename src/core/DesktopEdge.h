#pragma once

// 桌面四条边框（屏幕可用区域）与「贴边立绘」的**纯逻辑**：
//   - 判定桌宠窗口当前贴合了哪条边框；
//   - 给出该边框对应的立绘 pose（已登记在 PoseNames.h 的 kPoses 中）。
//
// 零 Qt 依赖（只用整型几何），可脱离界面单测；表现层实现见
// src/view/PoseView.cpp 与 src/view/PetWindow.cpp，设计说明见 docs/PRESENTATION.md §3.1。

namespace whalepet::core {

// 桌面（屏幕可用区域）的四条边框；None = 未贴边（常规居中显示）
enum class DesktopEdge {
    None = 0,
    Top,    // 上边框
    Bottom, // 下边框
    Left,   // 左边框
    Right   // 右边框
};

// 边框 → 贴边立绘 pose。四张立绘的语义（docs/PRESENTATION.md §1）：
//   Top    → home-bottom     （倒向探头，贴合上边框）
//   Bottom → home-peek       （横向探头，贴合下边框）
//   Left   → settings-peek   （竖向探头，贴合左边框）
//   Right  → workbench-peek  （竖向探头，贴合右边框）
// None 返回 nullptr（不换图）。
inline const char *edgePoseKey(DesktopEdge edge)
{
    switch (edge) {
    case DesktopEdge::Top:
        return "home-bottom";
    case DesktopEdge::Bottom:
        return "home-peek";
    case DesktopEdge::Left:
        return "settings-peek";
    case DesktopEdge::Right:
        return "workbench-peek";
    case DesktopEdge::None:
        break;
    }
    return nullptr;
}

// 判定窗口贴合了哪条边框。
//   - 矩形以 (x, y, w, h) 给出，(x + w) / (y + h) 为**排他**的下/右边界（与 QRect 一致）；
//   - 窗口某条边到桌面同侧边框的距离 <= thresholdPx 即视为「贴合」；
//   - 多条同时满足时取最近的一条，距离相同按 上 → 下 → 左 → 右 的顺序取先者；
//   - 超出阈值的一律不算（返回 None）。
//
// 边界情形：窗口被拖出桌面之外时距离可为负，仍按绝对值比较（由调用方负责夹回可见区域）。
inline DesktopEdge detectDesktopEdge(int winX, int winY, int winW, int winH,
                                     int areaX, int areaY, int areaW, int areaH,
                                     int thresholdPx)
{
    // 负距离 = 越过边框（窗口被拖出桌面）；取绝对值统一参与比较
    const int toTop = winY - areaY;
    const int toBottom = (areaY + areaH) - (winY + winH);
    const int toLeft = winX - areaX;
    const int toRight = (areaX + areaW) - (winX + winW);

    struct Candidate {
        DesktopEdge edge;
        int distance; // 绝对值
    };
    const Candidate candidates[] = {
        { DesktopEdge::Top, toTop < 0 ? -toTop : toTop },
        { DesktopEdge::Bottom, toBottom < 0 ? -toBottom : toBottom },
        { DesktopEdge::Left, toLeft < 0 ? -toLeft : toLeft },
        { DesktopEdge::Right, toRight < 0 ? -toRight : toRight },
    };

    DesktopEdge best = DesktopEdge::None;
    int bestDistance = 0;
    for (const Candidate &c : candidates) {
        if (c.distance > thresholdPx) {
            continue;
        }
        if (best == DesktopEdge::None || c.distance < bestDistance) {
            best = c.edge;
            bestDistance = c.distance;
        }
    }
    return best;
}

} // namespace whalepet::core
