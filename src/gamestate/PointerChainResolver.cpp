#include "gamestate/PointerChainResolver.h"

#include <QString>

#include <cmath>
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

// 地址加法：溢出即失败（溢出后的地址会绕回低地址，误读到无关内存）
bool addAddress(std::uint64_t base, std::uint64_t offset, std::uint64_t *out, QString *error,
                const char *what)
{
    if (base > UINT64_MAX - offset) {
        return fail(error, QStringLiteral("%1 地址溢出（0x%2 + 0x%3）")
                               .arg(QString::fromLatin1(what))
                               .arg(base, 0, 16)
                               .arg(offset, 0, 16));
    }
    *out = base + offset;
    return true;
}

// 浮点 → 整数：NaN / ±Inf / 超出 int64 范围在 C++ 中是**未定义行为**（UB），
// 必须先判定再转换（SECURITY-REVIEW.md §极端边界测试建议 6）。
bool toInteger(double raw, long long *out, QString *error, const char *what)
{
    if (std::isnan(raw) || std::isinf(raw)) {
        return fail(error, QStringLiteral("%1 读到非有限浮点值（%2），不做未定义转换")
                               .arg(QString::fromLatin1(what))
                               .arg(raw));
    }
    // 2^63 是 int64 的严格上界：等于或超过都不可表示
    constexpr double kInt64Limit = 9223372036854775808.0; // 2^63
    if (raw <= -kInt64Limit || raw >= kInt64Limit) {
        return fail(error, QStringLiteral("%1 超出 int64 范围（%2）")
                               .arg(QString::fromLatin1(what))
                               .arg(raw));
    }
    *out = static_cast<long long>(raw);
    return true;
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
    if (address == 0) {
        return fail(error, QStringLiteral("拒绝读取空地址"));
    }
    // 预算比较用减法形式：`已用 + 请求` 本身可能溢出 uint64
    if (size > m_maxBytesPerRound || m_bytesThisRound > m_maxBytesPerRound - size) {
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

    const QString name = QString::fromStdString(field.name);
    std::uint64_t address = 0;
    if (!addAddress(staticRoot, field.chain.front(), &address, error,
                    qPrintable(QStringLiteral("字段 %1 的静态根").arg(name)))) {
        return false;
    }
    for (std::size_t i = 1; i < field.chain.size(); ++i) {
        std::uint64_t pointer = 0;
        if (!readPointer(address, &pointer, error)) {
            return false;
        }
        if (pointer == 0) {
            return fail(error, QStringLiteral("字段 %1 的第 %2 跳得到空指针").arg(name).arg(i));
        }
        // 指针 + 偏移同样可能溢出（野指针值 + 大偏移会绕回）
        if (!addAddress(pointer, field.chain[i], &address, error,
                        qPrintable(QStringLiteral("字段 %1 的第 %2 跳").arg(name).arg(i)))) {
            return false;
        }
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
        // NaN / ±Inf / 超出 int64：一律明确失败，不做未定义转换
        if (!toInteger(value.number, &value.integer, error, "float 字段")) {
            return false;
        }
        value.boolean = raw != 0.0f;
        break;
    }
    case GameFieldKind::Double: {
        double raw = 0.0;
        if (!readRaw(address, &raw, sizeof(raw), error)) {
            return false;
        }
        value.number = raw;
        if (!toInteger(raw, &value.integer, error, "double 字段")) {
            return false;
        }
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
    std::uint64_t address = 0;
    if (!addAddress(base, validation.magicOffset, &address, error, "魔数")) {
        return false;
    }
    std::uint32_t magic = 0;
    if (m_reader == nullptr || address == 0 || !m_reader->read(address, &magic, sizeof(magic))) {
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
