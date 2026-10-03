#include "gamestate/PointerChainResolver.h"

#include <QString>

#include <cstring>

namespace whalepet::gamestate {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

} // namespace

PointerChainResolver::PointerChainResolver(IGameMemoryReader *reader)
    : m_reader(reader)
{
}

bool PointerChainResolver::readRaw(std::uint64_t address, void *buffer, std::size_t size,
                                   QString *error) const
{
    if (m_reader == nullptr) {
        return fail(error, QStringLiteral("读取器未绑定"));
    }
    if (m_bytesThisRound + size > m_maxBytesPerRound) {
        return fail(error, QStringLiteral("超出单轮字节预算（%1 + %2 > %3）")
                               .arg(m_bytesThisRound)
                               .arg(size)
                               .arg(m_maxBytesPerRound));
    }
    if (!m_reader->read(address, buffer, size)) {
        return fail(error, QStringLiteral("读取 0x%1 失败：%2")
                               .arg(address, 0, 16)
                               .arg(m_reader->lastError()));
    }
    m_bytesThisRound += size;
    return true;
}

bool PointerChainResolver::readPointer(std::uint64_t address, std::uint64_t *out,
                                       QString *error) const
{
    std::uint64_t value = 0;
    if (!readRaw(address, &value, sizeof(value), error)) {
        return false;
    }
    *out = value;
    return true;
}

bool PointerChainResolver::resolve(const GameFieldSpec &field, std::uint64_t staticRoot,
                                   std::uint64_t *outAddress, QString *error) const
{
    if (outAddress == nullptr) {
        return fail(error, QStringLiteral("输出地址为空"));
    }
    if (field.chain.empty()) {
        return fail(error, QStringLiteral("字段 %1 的 chain 为空").arg(QString::fromStdString(field.name)));
    }
    const int jumps = static_cast<int>(field.chain.size()) - 1;
    if (jumps > m_maxJumps) {
        return fail(error, QStringLiteral("字段 %1 的跳数 %2 超过上限 %3")
                               .arg(QString::fromStdString(field.name))
                               .arg(jumps)
                               .arg(m_maxJumps));
    }
    if (staticRoot == 0) {
        return fail(error, QStringLiteral("字段 %1 的静态根为 0（模块基址未解析）")
                               .arg(QString::fromStdString(field.name)));
    }

    std::uint64_t address = staticRoot + field.chain.front();
    for (std::size_t i = 1; i < field.chain.size(); ++i) {
        std::uint64_t pointer = 0;
        if (!readPointer(address, &pointer, error)) {
            return false;
        }
        if (pointer == 0) {
            return fail(error, QStringLiteral("字段 %1 的第 %2 跳得到空指针")
                                   .arg(QString::fromStdString(field.name))
                                   .arg(i));
        }
        address = pointer + field.chain[i];
    }
    *outAddress = address;
    return true;
}

bool PointerChainResolver::readField(const GameFieldSpec &field, std::uint64_t staticRoot,
                                     GameFieldValue *out, QString *error)
{
    if (out == nullptr) {
        return fail(error, QStringLiteral("输出值为空"));
    }
    std::uint64_t address = 0;
    if (!resolve(field, staticRoot, &address, error)) {
        return false;
    }

    const GameFieldKind kind = gameFieldKindFromId(field.kind);
    GameFieldValue value;
    switch (kind) {
    case GameFieldKind::Int32: {
        std::int32_t raw = 0;
        if (!readRaw(address, &raw, sizeof(raw), error)) {
            return false;
        }
        value.integer = raw;
        value.number = static_cast<double>(raw);
        value.boolean = raw != 0;
        break;
    }
    case GameFieldKind::Int64: {
        std::int64_t raw = 0;
        if (!readRaw(address, &raw, sizeof(raw), error)) {
            return false;
        }
        value.integer = raw;
        value.number = static_cast<double>(raw);
        value.boolean = raw != 0;
        break;
    }
    case GameFieldKind::Float: {
        float raw = 0.0f;
        if (!readRaw(address, &raw, sizeof(raw), error)) {
            return false;
        }
        value.number = static_cast<double>(raw);
        value.integer = static_cast<long long>(raw);
        value.boolean = raw != 0.0f;
        break;
    }
    case GameFieldKind::Double: {
        double raw = 0.0;
        if (!readRaw(address, &raw, sizeof(raw), error)) {
            return false;
        }
        value.number = raw;
        value.integer = static_cast<long long>(raw);
        value.boolean = raw != 0.0;
        break;
    }
    case GameFieldKind::Bool: {
        unsigned char raw = 0;
        if (!readRaw(address, &raw, sizeof(raw), error)) {
            return false;
        }
        value.boolean = raw != 0;
        value.integer = value.boolean ? 1 : 0;
        value.number = value.boolean ? 1.0 : 0.0;
        break;
    }
    case GameFieldKind::Utf16: {
        char16_t buffer[kGameUtf16MaxCodeUnits] = {};
        if (!readRaw(address, buffer, sizeof(buffer), error)) {
            return false;
        }
        std::size_t length = 0;
        while (length < kGameUtf16MaxCodeUnits && buffer[length] != u'\0') {
            ++length;
        }
        value.text = QString::fromUtf16(buffer, static_cast<qsizetype>(length)).toStdString();
        break;
    }
    case GameFieldKind::Unknown:
        return fail(error, QStringLiteral("字段 %1 的 kind 非法：%2")
                               .arg(QString::fromStdString(field.name),
                                    QString::fromStdString(field.kind)));
    }

    *out = value;
    return true;
}

bool PointerChainResolver::verifyMagic(std::uint64_t base, const GameProfile::Validation &validation,
                                       QString *error) const
{
    if (!validation.hasMagic) {
        return true;
    }
    if (base == 0) {
        return fail(error, QStringLiteral("魔数校验失败：模块基址为 0"));
    }
    const std::uint64_t address = base + validation.magicOffset;
    std::uint32_t magic = 0;
    if (m_reader == nullptr || !m_reader->read(address, &magic, sizeof(magic))) {
        return fail(error, QStringLiteral("魔数读取失败 @0x%1").arg(address, 0, 16));
    }
    if (magic != validation.magic) {
        return fail(error, QStringLiteral("魔数不匹配 @0x%1：期望 0x%2，实际 0x%3")
                               .arg(address, 0, 16)
                               .arg(validation.magic, 8, 16, QLatin1Char('0'))
                               .arg(magic, 8, 16, QLatin1Char('0')));
    }
    return true;
}

} // namespace whalepet::gamestate
