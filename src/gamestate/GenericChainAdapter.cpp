#include "gamestate/GenericChainAdapter.h"

#include <QString>

namespace whalepet::gamestate {

GenericChainAdapter::GenericChainAdapter(std::unique_ptr<IGameMemoryReader> reader)
    : m_reader(reader ? std::move(reader) : std::make_unique<Win32GameMemoryReader>())
{
}

GenericChainAdapter::~GenericChainAdapter()
{
    detach();
}

bool GenericChainAdapter::attach(const GameProfile &profile, QString *error)
{
    m_sampler.reset();
    m_reader->detach();
    m_profile = profile;

    if (!m_reader->attach(m_profile, error)) {
        return false;
    }
    m_sampler = std::make_unique<ChainSampler>(m_reader.get(), &m_profile);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void GenericChainAdapter::detach()
{
    m_sampler.reset();
    if (m_reader != nullptr) {
        m_reader->detach();
    }
}

bool GenericChainAdapter::attached() const
{
    return m_reader != nullptr && m_reader->attached();
}

bool GenericChainAdapter::read(core::GameSample *out, QString *error)
{
    if (out == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("输出为空");
        }
        return false;
    }
    out->available = false;
    if (!attached() || m_sampler == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("目标进程未连接");
        }
        return false;
    }
    return m_sampler->sample(out, error);
}

} // namespace whalepet::gamestate
