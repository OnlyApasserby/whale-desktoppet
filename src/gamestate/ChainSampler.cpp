#include "gamestate/ChainSampler.h"

#include <QString>

#include <string>

namespace whalepet::gamestate {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

// 字段名 → GameSample 成员（名字取 profile.fields[].name；含常用别名）
void applyField(core::GameSample *out, const std::string &name, const GameFieldValue &value)
{
    if (name == "hp") {
        out->hp = value.number;
    } else if (name == "hpMax" || name == "maxHp") {
        out->hpMax = value.number;
    } else if (name == "gold") {
        out->gold = value.integer;
    } else if (name == "level") {
        out->level = static_cast<int>(value.integer);
    } else if (name == "posX" || name == "x") {
        out->posX = value.number;
    } else if (name == "posY" || name == "y") {
        out->posY = value.number;
    } else if (name == "mapName" || name == "map") {
        out->mapName = value.text;
    } else if (name == "specialScene") {
        out->specialScene = static_cast<int>(value.integer);
    }
    // 其它字段：仍会读取并校验（跳失败 → 本轮作废），只是不映射到 GameSample
}

} // namespace

ChainSampler::ChainSampler(IGameMemoryReader *reader, const GameProfile *profile)
    : m_reader(reader)
    , m_profile(profile)
    , m_resolver(reader)
{
}

void ChainSampler::reset()
{
    m_error.clear();
    m_resolver.resetFailures();
    m_resolver.resetRoundBudget();
}

bool ChainSampler::sample(core::GameSample *out, QString *error)
{
    if (out == nullptr) {
        return fail(error, QStringLiteral("输出为空"));
    }
    out->available = false;

    if (m_reader == nullptr || m_profile == nullptr) {
        m_error = QStringLiteral("采样器未绑定读取器 / 档案");
        return fail(error, m_error);
    }
    if (!m_reader->attached()) {
        m_error = QStringLiteral("目标进程未连接");
        return fail(error, m_error);
    }
    if (m_resolver.invalidated()) {
        m_error = QStringLiteral("档案已失效（连续 %1 次读取失败）").arg(m_resolver.consecutiveFailures());
        return fail(error, m_error);
    }

    const std::uint64_t moduleBase = m_reader->moduleBase(m_profile->module);
    if (moduleBase == 0) {
        m_resolver.noteFailure();
        m_error = QStringLiteral("模块基址未找到：%1").arg(QString::fromStdString(m_profile->module));
        return fail(error, m_error);
    }
    const std::uint64_t staticRoot = moduleBase + m_profile->moduleBaseOffset;

    m_resolver.setMaxJumps(m_profile->validation.maxJumps);
    m_resolver.setMaxBytesPerRound(m_profile->maxBytesPerRound);
    m_resolver.resetRoundBudget();

    QString reason;
    if (!m_resolver.verifyMagic(staticRoot, m_profile->validation, &reason)) {
        m_resolver.noteFailure();
        m_error = reason;
        return fail(error, reason);
    }

    core::GameSample sample;
    for (const GameFieldSpec &spec : m_profile->fields) {
        GameFieldValue value;
        if (!m_resolver.readField(spec, staticRoot, &value, &reason)) {
            m_resolver.noteFailure();
            m_error = reason;
            return fail(error, reason);
        }
        applyField(&sample, spec.name, value);
    }

    sample.available = true;
    m_resolver.noteSuccess();
    m_error.clear();
    *out = sample;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace whalepet::gamestate
