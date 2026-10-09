#pragma once

// 陪玩侧「通用聚合」数据源（EX4）。
//
// 与具体玩法无关：向宿主索取一份「当前可能活跃」的小游戏自描述源候选，
// 逐个询问中立快照（core::GameSnapshot），取首个可用者。
// 因此**新增小游戏不需要改动本文件** —— 只要新游戏的视图实现了
// IMiniGameCompanionSource，它就会自动被发现并接入陪玩。
//
// 与 IGameCompanionSource 的关系：
//   * readSnapshot() 走中立快照通道（小游戏陪玩的主路径）；
//   * read() 由中立快照折算为 GameSample，兼容既有管线（信号参数 / Context 投影）。

#include "viewmodel/IGameCompanionSource.h"

#include <QList>

#include <functional>

namespace whalepet {
class IMiniGameCompanionSource;
} // namespace whalepet

namespace whalepet::viewmodel {

// 候选提供者：由宿主给出「当前可能活跃」的小游戏自描述源（已完成 dynamic_cast + 可见性筛选）。
using MiniGameCompanionCandidates = std::function<QList<IMiniGameCompanionSource *>()>;

class MiniGameCompanionSource : public IGameCompanionSource {
public:
    explicit MiniGameCompanionSource(MiniGameCompanionCandidates candidates);

    bool attach(QString *error) override;
    void detach() override;
    bool attached() const override;
    bool read(core::GameSample *out, QString *error) override;
    bool readSnapshot(core::GameSnapshot *out, QString *error) override;
    bool invalidated() const override { return false; }

private:
    MiniGameCompanionCandidates m_candidates;
    bool m_attached = false;
};

} // namespace whalepet::viewmodel
