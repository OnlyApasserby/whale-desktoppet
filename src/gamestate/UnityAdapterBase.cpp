#include "gamestate/UnityAdapterBase.h"

#include "gamestate/Win32GameMemoryReader.h"

namespace whalepet::gamestate {

UnityAdapterBase::UnityAdapterBase(std::unique_ptr<IGameMemoryReader> reader, std::string engineId,
                                   std::string fallbackModule)
    : m_reader(reader ? std::move(reader) : std::make_unique<Win32GameMemoryReader>()),
      m_engineId(std::move(engineId)),
      m_fallbackModule(std::move(fallbackModule))
{
}

UnityAdapterBase::~UnityAdapterBase() = default;

bool UnityAdapterBase::attach(const GameProfile &profile, QString *error)
{
    if (profile.engine != m_engineId) {
        if (error != nullptr) {
            *error = QStringLiteral("适配器与档案引擎不匹配：适配器期望 %1，档案为 %2")
                         .arg(QString::fromStdString(m_engineId),
                              QString::fromStdString(profile.engine));
        }
        return false;
    }
    if (!m_reader->attach(profile, error)) {
        if (error != nullptr) {
            *error += QLatin1Char(' ') + backendHint();
        }
        return false;
    }
    m_profile = profile;
    if (m_profile.module.empty()) {
        m_profile.module = m_fallbackModule;
    }
    m_sampler = std::make_unique<ChainSampler>(m_reader.get(), &m_profile);
    return true;
}

void UnityAdapterBase::detach()
{
    m_sampler.reset();
    if (m_reader != nullptr) {
        m_reader->detach();
    }
}

bool UnityAdapterBase::attached() const
{
    return m_reader != nullptr && m_reader->attached();
}

bool UnityAdapterBase::read(core::GameSample *out, QString *error)
{
    if (m_sampler == nullptr || !attached()) {
        if (out != nullptr) {
            out->available = false;
        }
        if (error != nullptr) {
            *error = QStringLiteral("适配器尚未 attach");
        }
        return false;
    }
    const bool ok = m_sampler->sample(out, error);
    if (!ok && m_reader->moduleBase(m_profile.module) == 0) {
        // 以「模块不存在」这一最常见的一次性原因补充引擎提示（后端可能选错）。
        if (error != nullptr) {
            *error += QLatin1Char(' ') + backendHint();
        }
    }
    return ok;
}

} // namespace whalepet::gamestate
