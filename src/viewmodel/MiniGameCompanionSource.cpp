#include "viewmodel/MiniGameCompanionSource.h"

#include "core/MiniGameCompanion.h"
#include "minigame/MiniGameCompanionSource.h"

#include <QDateTime>

#include <utility>

namespace whalepet::viewmodel {

MiniGameCompanionSource::MiniGameCompanionSource(MiniGameCompanionCandidates candidates)
    : m_candidates(std::move(candidates))
{
}

bool MiniGameCompanionSource::attach(QString *error)
{
    if (!m_candidates) {
        if (error != nullptr) {
            *error = QStringLiteral("未提供小游戏陪玩候选访问器");
        }
        return false;
    }
    m_attached = true;
    return true;
}

void MiniGameCompanionSource::detach()
{
    m_attached = false;
}

bool MiniGameCompanionSource::attached() const
{
    return m_attached;
}

bool MiniGameCompanionSource::readSnapshot(core::GameSnapshot *out, QString *)
{
    if (out == nullptr || !m_candidates) {
        return false;
    }
    // 通用聚合：候选顺序由宿主给定（活跃窗口），取首个「自描述为可用」者。
    // 这里**没有任何具体玩法的分支** —— 新增小游戏自动被发现。
    const QList<IMiniGameCompanionSource *> list = m_candidates();
    for (IMiniGameCompanionSource *source : list) {
        if (source == nullptr) {
            continue;
        }
        core::GameSnapshot snapshot;
        if (source->companionSnapshot(&snapshot) && snapshot.available) {
            snapshot.nowMs = QDateTime::currentMSecsSinceEpoch();
            *out = snapshot;
            return true;
        }
    }
    return false;
}

bool MiniGameCompanionSource::read(core::GameSample *out, QString *error)
{
    core::GameSnapshot snapshot;
    if (!readSnapshot(&snapshot, error)) {
        return false;
    }
    *out = core::gameSampleFromSnapshot(snapshot);
    return true;
}

} // namespace whalepet::viewmodel
