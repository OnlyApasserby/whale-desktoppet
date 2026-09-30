#include "viewmodel/PosePresenter.h"

#include "view/PoseView.h"
#include "view/SpeechBubble.h"

#include <QDebug>
#include <QFile>

namespace whalepet {

PosePresenter::PosePresenter(PoseView *view, SpeechBubble *bubble, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_bubble(bubble)
{
}

std::size_t PosePresenter::loadBundledLines(core::LineTable &table)
{
    QFile file(QStringLiteral(":/lines/lines.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 降级：无台词资源时仍有立绘与动效，只是不说话（docs/TESTING.md §7 要求写明原因）
        qWarning() << "[PosePresenter] 台词资源缺失，降级为不说话:" << file.fileName();
        return 0;
    }
    const std::size_t count = table.loadFromText(file.readAll().toStdString());
    if (count == 0) {
        qWarning() << "[PosePresenter] 台词资源为空或格式不合法:" << file.fileName();
    }
    return count;
}

void PosePresenter::present(const core::PoseResult &result)
{
    if (result.pose.empty()) {
        return;
    }

    if (m_view != nullptr) {
        m_view->setPose(QString::fromStdString(result.pose));
        // 序号去重：controller 每 tick 重推同一结果时 fxSerial 不变 → 特效只播一次
        // 注意：序号在提交前即消费——若该次被 PoseView 的 500ms 强制间隔丢弃，
        // 不会在下一个 tick 补播（「丢弃而非排队」，见 docs/ROADMAP-P2.md §A）。
        if (result.fx != core::Fx::None && result.fxSerial != 0
            && result.fxSerial != m_lastFxSerial) {
            m_lastFxSerial = result.fxSerial;
            m_view->playFx(result.fx);
        }
    }

    if (result.lineKey.empty() || m_bubble == nullptr || m_lines == nullptr) {
        return;
    }
    // 同理：一次操作只播一次台词。台词文本是**播放时**才随机选取的，
    // 因此重复重放同一 lineKey 会不断换句——必须靠序号拦住。
    if (result.lineSerial == 0 || result.lineSerial == m_lastLineSerial) {
        return;
    }
    m_lastLineSerial = result.lineSerial;

    const QString text = QString::fromStdString(m_lines->pick(result.lineKey, m_rng));
    if (!text.isEmpty()) {
        // 流式输出；新台词会打断上一条（连点只保留最后一次操作对应的台词）
        m_bubble->startStream(text, result.ttlMs);
    }
}

void PosePresenter::hideBubble()
{
    if (m_bubble != nullptr) {
        m_bubble->hideLine();
    }
}

} // namespace whalepet
