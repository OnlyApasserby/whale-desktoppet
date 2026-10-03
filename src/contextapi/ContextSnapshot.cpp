#include "contextapi/ContextSnapshot.h"

namespace whalepet::contextapi {

namespace {

// JSON 数字统一走 double：QJsonValue 只支持 double（qint64 直接插入会被截断语义）
double asNumber(qint64 value)
{
    return static_cast<double>(value);
}

} // namespace

qint64 ContextSnapshot::uptimeMs(qint64 nowMs) const
{
    const qint64 now = (nowMs > 0) ? nowMs : generatedAtMs;
    const qint64 uptime = now - startedAtMs;
    return (uptime > 0) ? uptime : 0;
}

QJsonObject ContextSnapshot::envJson() const
{
    QJsonObject env;
    env.insert(QStringLiteral("available"), envAvailable);
    env.insert(QStringLiteral("appId"), appId);
    env.insert(QStringLiteral("windowTitle"), windowTitle);
    env.insert(QStringLiteral("category"), QString::fromLatin1(core::appCategoryId(category)));
    env.insert(QStringLiteral("idleMs"), asNumber(idleMs));
    env.insert(QStringLiteral("inputEvents"), inputEvents);
    env.insert(QStringLiteral("appSwitches"), appSwitches);
    env.insert(QStringLiteral("dwellMs"), asNumber(dwellMs));
    env.insert(QStringLiteral("systemPaused"), systemPaused);
    return env;
}

QJsonObject ContextSnapshot::workJson() const
{
    QJsonObject work;
    work.insert(QStringLiteral("state"), QString::fromLatin1(core::workStateId(workState)));
    work.insert(QStringLiteral("confidence"), workConfidence);
    work.insert(QStringLiteral("sinceMs"), asNumber(workSinceMs));
    // 便于 Agent 侧直接判断「现在是否应当保持安静」
    work.insert(QStringLiteral("focus"), core::workStateIsFocus(workState));
    return work;
}

QJsonObject ContextSnapshot::gameJson() const
{
    QJsonObject game;
    game.insert(QStringLiteral("available"), gameAvailable);
    game.insert(QStringLiteral("engine"), gameEngine);
    game.insert(QStringLiteral("mood"), QString::fromLatin1(core::gameMoodId(gameMoodState)));
    game.insert(QStringLiteral("confidence"), gameConfidence);
    game.insert(QStringLiteral("sinceMs"), asNumber(gameSinceMs));
    game.insert(QStringLiteral("specialScene"), gameSpecialScene);
    // 便于 Agent 侧直接判断「现在是否应当保持安静」（CG/影片/对话演出期间为 true）
    game.insert(QStringLiteral("silent"), gameSilent);
    return game;
}

QJsonObject ContextSnapshot::petJson() const
{
    QJsonObject pet;
    pet.insert(QStringLiteral("available"), petAvailable);
    pet.insert(QStringLiteral("level"), level);
    pet.insert(QStringLiteral("exp"), exp);
    pet.insert(QStringLiteral("mood"), mood);
    pet.insert(QStringLiteral("affinity"), affinity);
    pet.insert(QStringLiteral("satiety"), satiety);
    pet.insert(QStringLiteral("bondLevel"), bondLevel);
    pet.insert(QStringLiteral("companionMs"), asNumber(companionMs));
    return pet;
}

QJsonObject ContextSnapshot::sessionJson(qint64 nowMs) const
{
    QJsonObject session;
    session.insert(QStringLiteral("startedAtMs"), asNumber(startedAtMs));
    session.insert(QStringLiteral("uptimeMs"), asNumber(uptimeMs(nowMs)));
    session.insert(QStringLiteral("samples"), asNumber(samples));
    session.insert(QStringLiteral("interactions"), asNumber(interactions));
    session.insert(QStringLiteral("workStateChanges"), asNumber(workStateChanges));
    session.insert(QStringLiteral("gameStateChanges"), asNumber(gameStateChanges));
    return session;
}

QJsonObject ContextSnapshot::toJson(qint64 nowMs) const
{
    QJsonObject out;
    out.insert(QStringLiteral("apiVersion"), apiVersion);
    out.insert(QStringLiteral("appVersion"), appVersion);
    out.insert(QStringLiteral("generatedAtMs"), asNumber((nowMs > 0) ? nowMs : generatedAtMs));
    out.insert(QStringLiteral("env"), envJson());
    out.insert(QStringLiteral("work"), workJson());
    out.insert(QStringLiteral("game"), gameJson());
    out.insert(QStringLiteral("pet"), petJson());
    out.insert(QStringLiteral("session"), sessionJson(nowMs));
    return out;
}

} // namespace whalepet::contextapi
