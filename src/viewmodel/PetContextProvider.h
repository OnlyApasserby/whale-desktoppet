#pragma once

// IContextProvider 的 view 侧实现（docs/PLUGIN-ARCHITECTURE.md §3.1）：
//   把 PetController / GrowthService / EnvironmentService / WorkStateService 聚合为 ContextSnapshot。
//
// 为什么放在 view 侧：contextapi 不得反向依赖 view（否则循环）。本类实现其接口，
// 由组合根（PetWindow）注入给 ContextApiService。
//
// 数据缺失约定：依赖全部可空；对应分组保持空 / 0 且 `available == false`，**不伪造数据**。

#include "contextapi/IContextProvider.h"

#include <QObject>

#include <cstdint>

namespace whalepet {
class PetController; // 注意：PetController 位于 whalepet 命名空间（非 viewmodel）
}

namespace whalepet::viewmodel {

class EnvironmentService;
class GameCompanionService;
class GrowthService;
class WorkStateService;

class PetContextProvider : public QObject, public contextapi::IContextProvider {
    Q_OBJECT
public:
    explicit PetContextProvider(QObject *parent = nullptr);
    ~PetContextProvider() override = default;

    void setController(PetController *controller);
    void setGrowth(GrowthService *growth);
    void setEnvironment(EnvironmentService *environment);
    void setWorkState(WorkStateService *workState);
    // EX1.4：游戏陪玩（未接入或未启用时 game.available == false，不伪造数据）
    void setGameCompanion(GameCompanionService *gameCompanion);

    contextapi::ContextSnapshot snapshot() const override;

    qint64 interactionCount() const { return m_interactions; }

private:
    PetController *m_controller = nullptr; // 非拥有（用于交互计数）
    GrowthService *m_growth = nullptr;     // 非拥有
    EnvironmentService *m_environment = nullptr; // 非拥有
    WorkStateService *m_workState = nullptr;     // 非拥有
    GameCompanionService *m_gameCompanion = nullptr; // 非拥有（EX1.4）

    qint64 m_startedAtMs = 0;
    qint64 m_interactions = 0;
};

} // namespace whalepet::viewmodel
