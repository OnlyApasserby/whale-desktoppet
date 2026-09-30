#pragma once

// PetController：P2 阶段的总调度（docs/ARCHITECTURE.md §3 ViewModel 层）。
//
// 「用户/定时事件 → PetStateMachine → PosePresenter → 立绘与气泡」的唯一串联点。
// View（PetWindow）只负责把鼠标手势翻译成这里的方法调用，并读取只读状态；
// 状态机与台词表都是纯逻辑，可脱离界面单测。

#include "core/IRandom.h"
#include "core/LineTable.h"
#include "core/PetStateMachine.h"
#include "core/PetTypes.h"

#include <QElapsedTimer>
#include <QObject>

class QTimer;

namespace whalepet {

namespace viewmodel {
class GrowthService;
} // namespace viewmodel

class PosePresenter;
class PoseView;
class SpeechBubble;

class PetController : public QObject {
    Q_OBJECT
public:
    PetController(PoseView *view, SpeechBubble *bubble, QObject *parent = nullptr);

    // 注入养成服务（P3）：交互事件按 whale 数值表落到养成状态，
    // 升级/羁绊提升回灌为 core::EventType::LevelUp 驱动庆祝姿态。
    // 允许为空（P1/P2 的既有测试即不注入），此时行为与 P2 完全一致。
    void setGrowthService(viewmodel::GrowthService *growth);

    // 启动/停止定时驱动（tick + 系统时钟）
    void start();
    void stop();
    bool running() const;

    // ---- 输入 ----
    void handleClick(core::Zone zone);        // 分区单击（内部含三连击判定）
    void handleDragBegin();
    void handleDragEnd();
    void handleMenuAction(core::EventType type); // Feed / Tease / Praise

    // 外部系统事件（P3 起由 GrowthService / ChatService 驱动）
    void handleEvent(core::EventType type);
    void handleKeywordHit(const QString &keyword);

    // 游戏/设置面板打开时抑制主动小剧场
    void setSuppressed(bool suppressed);

    core::PetStateMachine &stateMachine() { return m_sm; }
    const core::LineTable &lineTable() const { return m_lines; }

private:
    // 分区/菜单事件 → 养成交互（无对应养成交互时不做任何事）
    void applyGrowthForZone(core::Zone zone);
    void applyGrowthForEvent(core::EventType type);

    void onTick();
    void onClockTick();
    void presentCurrent();

    qint64 nowMs() const { return m_clock.elapsed(); }

    core::SystemRandom m_rng;
    core::LineTable m_lines;
    core::PetStateMachine m_sm;
    PosePresenter *m_presenter = nullptr;
    viewmodel::GrowthService *m_growth = nullptr;

    QTimer *m_tickTimer = nullptr;
    QTimer *m_clockTimer = nullptr;
    QElapsedTimer m_clock;

    int m_clickStreak = 0;
    qint64 m_lastClickMs = -1;
    int m_lastHour = -1;
};

} // namespace whalepet
