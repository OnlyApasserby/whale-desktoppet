#pragma once

// PosePresenter：把状态机的**语义结果**翻译成 Qt 表现（docs/ARCHITECTURE.md §3）。
//
// 职责边界：
//   - 只消费 core::PoseResult（pose / lineKey / fx / ttlMs / 序号），不感知状态机内部；
//   - 不决定「什么时候切」——那是 controller 的事；
//   - 台词文本来自外部语料（assets/lines/lines.txt → core::LineTable），不硬编码。

#include "core/LineTable.h"
#include "core/PetTypes.h"

#include <QObject>
#include <QString>

#include <cstdint>

namespace whalepet {

class PoseView;
class SpeechBubble;

class PosePresenter : public QObject {
    Q_OBJECT
public:
    // 目标对象由 PetWindow 持有，Presenter 不接管所有权
    PosePresenter(PoseView *view, SpeechBubble *bubble, QObject *parent = nullptr);

    void setLineTable(core::LineTable *table) { m_lines = table; }
    void setRandom(core::IRandom *rng) { m_rng = rng; } // 可与状态机共用同一随机源

    // 语义结果 → 立绘 / 特效 / 气泡
    void present(const core::PoseResult &result);

    void hideBubble();

    // 加载内建台词资源；缺失时降级为空表并告警（不静默）
    static std::size_t loadBundledLines(core::LineTable &table);

private:
    PoseView *m_view = nullptr;
    SpeechBubble *m_bubble = nullptr;
    core::LineTable *m_lines = nullptr;
    core::IRandom *m_rng = nullptr;

    // 表现批次去重（docs/ROADMAP-P2.md 增补 §0）：
    // controller 每 tick 都会重推同一个缓存结果，只有序号变化才代表「新的一次表现」。
    // 没有这层去重时，一次三连击会按 tick 连播约 10 次粒子、一句台词会被每 200ms 换掉。
    std::uint32_t m_lastFxSerial = 0;
    std::uint32_t m_lastLineSerial = 0;
};

} // namespace whalepet
