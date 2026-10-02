#pragma once

// 立绘与动效的几何/时间常量。
//
// 重要前提：93 张立绘已统一缩放为 256x256（VP8X webp），画布尺寸完全一致。
// 因此窗口使用**固定尺寸**，切换立绘不再触发窗口 resize，
// 「过渡遮断」只需作用于内容层（缩放/位移），不会出现窗口抖动与残影。

#include <QtGlobal>

namespace whalepet {

// ---- 立绘几何 ----
inline constexpr int kPosePixels = 256;                          // 源图统一边长
inline constexpr int kPetDisplaySize = 200;                      // 立绘基准显示边长（逻辑像素）
inline constexpr int kPetMargin = 20;                            // 四周留白，容纳呼吸/旋转/位移
inline constexpr int kPetWindowSize = kPetDisplaySize + kPetMargin * 2; // 240，固定不变

// ---- 帧驱动 ----
inline constexpr int kFrameIntervalActiveMs = 16;   // 过渡 / 粒子 / 弹簧未收敛
inline constexpr int kFrameIntervalIdleMs = 40;     // 仅呼吸

// ---- 呼吸 ----
inline constexpr int kBreathPeriodMs = 2600;
inline constexpr qreal kBreathAmp = 0.025;          // 幅度 ±2.5%

// ---- 立绘切换（过渡遮断）----
inline constexpr int kTransitionMs = 260;
inline constexpr qreal kTransitionSquash = 0.88;    // 中点下压系数
inline constexpr qreal kTransitionSwapAt = 0.5;     // 换图时刻（进度比例）

// ---- 点击反馈 ----
inline constexpr int kClickFeedbackMs = 220;
inline constexpr qreal kClickSquash = 0.07;
inline constexpr qreal kClickNudgePx = 6.0;

// ---- 拖拽摇摆 / 惯性滑行 ----
inline constexpr qreal kMaxSwayDeg = 8.0;           // 摇摆角上限
inline constexpr int kSwayTauMs = 110;              // 摇摆一阶插值时间常数
inline constexpr qreal kSwayDegPerPx = 0.06;        // 每像素水平位移对应的角度（100px ≈ 6°）
inline constexpr int kInertiaMs = 420;              // 惯性滑行总时长（滑出 → 弹回）
inline constexpr qreal kInertiaFactor = 0.016;      // 惯性位移(px) = 松手速度(px/s) × 系数
inline constexpr qreal kInertiaMaxOffsetPx = 18.0;  // 惯性位移上限：受 kPetMargin(=20) 约束，避免出画
inline constexpr qreal kInertiaMinSpeed = 900.0;    // px/s，低于则不做惯性

// ---- 粒子特效 ----
inline constexpr int kMaxParticles = 48;
inline constexpr int kParticleLifeMs = 900;
inline constexpr int kParticleLifeLongMs = 1400;
inline constexpr qreal kParticleGravity = 0.00022;  // px/ms^2
inline constexpr int kHeartCount = 6;               // 夸夸：上浮爱心
inline constexpr int kStarCount = 12;               // 升级/成就：迸发星星
inline constexpr int kSparkCount = 26;              // 三连击：环绕碎钻

// ---- 特效触发间隔（ROADMAP-P2 增补 A）----
// 序号去重已保证「一次操作一次迸发」，此处再给一个强制下限：
// 500ms 内的新触发直接**丢弃**（不排队），避免急速连点造成的叠加与闪烁。
// 所有特效类型共用同一冷却；「点击反馈」（下压/上顶的缩放回弹）不在此限。
inline constexpr int kFxMinGapMs = 500;

// ---- 台词流式输出（ROADMAP-P2 增补 B）----
inline constexpr int kStreamCharIntervalMs = 40;    // 每字间隔 ≈ 25 字/秒
inline constexpr int kStreamPunctPauseMs = 120;     // 标点后的额外停顿

// ---- 分区命中（相对内容矩形高度比例）----
inline constexpr qreal kHeadZoneEnd = 0.40;
inline constexpr qreal kBellyZoneEnd = 0.78;

// ---- 桌面贴边（见 docs/PRESENTATION.md §3.1）----
// 窗口某条边到桌面（屏幕可用区域）同侧边框的距离 <= 该值即判定为「贴合该边框」，
// 立绘**立即**换成对应方向的探头立绘（不做过渡动画）；拖拽松手时按同一阈值吸附为完全贴合。
inline constexpr int kEdgeAttachPx = 20;
// 贴边立绘按源图的**可见内容包围盒**贴齐边框：alpha 不高于该值视为透明背景
// （webp 抗锯齿边缘会有极淡像素，留一档阈值避免包围盒被撑到整幅画布）。
inline constexpr int kPeekAlphaThreshold = 8;

// ---- 交互判定 ----
inline constexpr int kMultiClickWindowMs = 600;      // 三连击窗口
inline constexpr int kMultiClickCount = 3;

// ---- 台词气泡 ----
inline constexpr int kBubbleMaxWidth = 220;
inline constexpr int kBubbleGapPx = 6;               // 气泡与立绘间距
inline constexpr int kBubbleDefaultTtlMs = 4000;
inline constexpr int kBubbleScreenMarginPx = 8;

} // namespace whalepet
