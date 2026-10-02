#include "viewmodel/PetContextProvider.h"

#include "model/PetStateData.h"
#include "viewmodel/EnvironmentService.h"
#include "viewmodel/GrowthService.h"
#include "viewmodel/PetController.h"
#include "viewmodel/WorkStateService.h"

#include <QCoreApplication>
#include <QDateTime>

namespace whalepet::viewmodel {

PetContextProvider::PetContextProvider(QObject *parent)
    : QObject(parent)
    , m_startedAtMs(QDateTime::currentMSecsSinceEpoch())
{
}

void PetContextProvider::setController(PetController *controller)
{
    m_controller = controller;
    if (m_controller == nullptr) {
        return;
    }
    connect(m_controller, &PetController::interactionOccurred, this,
            [this](core::Interaction, qint64) { ++m_interactions; });
}

void PetContextProvider::setGrowth(GrowthService *growth)
{
    m_growth = growth;
}

void PetContextProvider::setEnvironment(EnvironmentService *environment)
{
    m_environment = environment;
}

void PetContextProvider::setWorkState(WorkStateService *workState)
{
    m_workState = workState;
}

contextapi::ContextSnapshot PetContextProvider::snapshot() const
{
    contextapi::ContextSnapshot out;
    out.apiVersion = QString::fromLatin1(contextapi::kContextApiVersion);
    out.appVersion = QCoreApplication::applicationVersion();
    out.generatedAtMs = QDateTime::currentMSecsSinceEpoch();
    out.startedAtMs = m_startedAtMs;

    if (m_environment != nullptr) {
        const core::EnvSample &sample = m_environment->lastSample();
        // 「可用」= 观察者可用 **且** 本次采样确实带回了数据（避免把空快照说成真实环境）
        out.envAvailable = m_environment->available() && !sample.isEmpty();
        out.appId = QString::fromStdString(sample.appId);
        out.windowTitle = QString::fromStdString(sample.windowTitle);
        out.category = sample.category;
        out.idleMs = sample.idleMs;
        out.inputEvents = sample.inputEvents;
        out.appSwitches = sample.appSwitches;
        out.dwellMs = sample.dwellMs;
        out.systemPaused = sample.systemPaused;
        out.samples = m_environment->sampleCount();
    }

    if (m_workState != nullptr) {
        const core::WorkStateSample &current = m_workState->current();
        // state == Unknown 时照实报告（不假装有数据）；workStateChanges 反映判定历史
        out.workState = current.state;
        out.workConfidence = current.confidence;
        out.workSinceMs = current.sinceMs;
        out.workStateChanges = m_workState->changeCount();
    }

    if (m_growth != nullptr) {
        const model::PetStateData &state = m_growth->state();
        out.petAvailable = true;
        out.level = state.level;
        out.exp = state.exp;
        out.mood = state.mood;
        out.affinity = state.affinity;
        out.satiety = state.satiety;
        out.bondLevel = state.bondLevel;
        out.companionMs = state.companionMs;
    }

    out.interactions = m_interactions;
    return out;
}

} // namespace whalepet::viewmodel
