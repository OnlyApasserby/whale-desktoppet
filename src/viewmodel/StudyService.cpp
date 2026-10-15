#include "viewmodel/StudyService.h"

#include "core/PythonImports.h"
#include "viewmodel/WorkService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>

#include <cstddef>
#include <string>
#include <vector>

namespace whalepet::viewmodel {

namespace {

const char *const kKindBlock = "block";
const char *const kKindExternal = "external";
const QString kExternalIdPrefix = QStringLiteral("external:");

std::string toStd(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

// "external:<n>" → n；不是该前缀 / 非正数返回 0
int externalIndexOf(const QString &id)
{
    if (!id.startsWith(kExternalIdPrefix)) {
        return 0;
    }
    bool ok = false;
    const int index = id.mid(kExternalIdPrefix.size()).toInt(&ok);
    return (ok && index > 0) ? index : 0;
}

// 清单 → JSON 数组文本（功能块条目只落 id / kind；外部条目另落 title / body）
QString itemsToJson(const QVector<StudyItem> &items)
{
    QJsonArray array;
    for (const StudyItem &item : items) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), item.id);
        object.insert(QStringLiteral("kind"), item.kind);
        if (item.isExternal()) {
            object.insert(QStringLiteral("title"), item.title);
            object.insert(QStringLiteral("body"), item.body);
        }
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

// JSON 数组文本 → 清单（无法识别的条目跳过：不猜测；同时给出下一个外部条目序号）
QVector<StudyItem> parseItemsJson(const QString &json, int *nextExternal)
{
    QVector<StudyItem> loaded;
    int next = 1;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isArray()) {
        for (const QJsonValue &value : doc.array()) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject object = value.toObject();
            const QString kind = object.value(QStringLiteral("kind")).toString();
            const QString id = object.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || (kind != QString::fromLatin1(kKindBlock)
                                 && kind != QString::fromLatin1(kKindExternal))) {
                continue;
            }
            bool duplicated = false;
            for (const StudyItem &existing : loaded) {
                if (existing.id == id) {
                    duplicated = true;
                    break;
                }
            }
            if (duplicated) {
                continue;
            }

            StudyItem item;
            item.id = id;
            item.kind = kind;
            if (item.isExternal()) {
                item.title = object.value(QStringLiteral("title")).toString();
                item.body = object.value(QStringLiteral("body")).toString();
                if (item.body.trimmed().isEmpty()) {
                    continue; // 空正文条目丢弃（宁缺毋滥）
                }
                const int index = externalIndexOf(id);
                if (index >= next) {
                    next = index + 1;
                }
            }
            loaded.push_back(item);
        }
    }
    if (nextExternal != nullptr) {
        *nextExternal = next;
    }
    return loaded;
}

} // namespace

StudyService::StudyService(QObject *parent)
    : QObject(parent)
{
}

void StudyService::setWorkService(WorkService *work)
{
    m_work = work;
}

QVector<core::PythonBlock> StudyService::availableBlocks() const
{
    if (m_work == nullptr) {
        return {};
    }
    return m_work->blocks();
}

QString StudyService::blockBody(const QString &blockId) const
{
    return (m_work == nullptr) ? QString() : m_work->blockBody(blockId);
}

const core::PythonBlock *StudyService::findBlock(const QString &id) const
{
    if (m_work == nullptr) {
        return nullptr;
    }
    const std::string wanted = toStd(id);
    for (const core::PythonBlock &block : m_work->blocks()) {
        if (block.id == wanted) {
            return &block;
        }
    }
    return nullptr;
}

QVector<StudyItem> StudyService::items() const
{
    QVector<StudyItem> out;
    out.reserve(m_items.size());
    for (const StudyItem &stored : m_items) {
        StudyItem item = stored;
        if (item.isBlock()) {
            const core::PythonBlock *block = findBlock(item.id);
            if (block != nullptr) {
                item.title = QString::fromStdString(block->title);
                item.category = QString::fromStdString(block->category);
                item.summary = QString::fromStdString(block->summary);
                item.requires = QString::fromStdString(block->requires);
            } else {
                // 源已失效：仍列出（便于用户自行移出），但不参与拼接
                item.title = item.id;
                item.category = QStringLiteral("已失效");
                item.summary.clear();
                item.requires.clear();
            }
        } else {
            item.category = QStringLiteral("外部");
            item.summary = QStringLiteral("手动导入的代码文本");
            if (item.title.isEmpty()) {
                item.title = item.id;
            }
        }
        out.push_back(item);
    }
    return out;
}

bool StudyService::contains(const QString &id) const
{
    for (const StudyItem &item : m_items) {
        if (item.id == id) {
            return true;
        }
    }
    return false;
}

int StudyService::importBlocks(const QStringList &blockIds)
{
    int added = 0;
    for (const QString &id : blockIds) {
        if (findBlock(id) == nullptr || contains(id)) {
            continue; // 未知 id / 重复导入：跳过（宁缺毋滥）
        }
        StudyItem item;
        item.id = id;
        item.kind = QString::fromLatin1(kKindBlock);
        m_items.push_back(item);
        ++added;
    }
    if (added > 0) {
        emit changed();
    }
    return added;
}

QString StudyService::addExternal(const QString &title, const QString &body)
{
    if (body.trimmed().isEmpty()) {
        return {};
    }
    StudyItem item;
    item.id = kExternalIdPrefix + QString::number(m_nextExternal);
    item.kind = QString::fromLatin1(kKindExternal);
    item.title = title.trimmed().isEmpty()
                     ? QStringLiteral("外部代码 ") + QString::number(m_nextExternal)
                     : title.trimmed();
    item.body = body;
    ++m_nextExternal;
    m_items.push_back(item);
    emit changed();
    return item.id;
}

int StudyService::removeItems(const QStringList &ids)
{
    if (ids.isEmpty()) {
        return 0;
    }
    int removed = 0;
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (!ids.contains(m_items.at(i).id)) {
            continue;
        }
        m_items.remove(i);
        ++removed;
    }
    if (removed > 0) {
        emit changed();
    }
    return removed;
}

void StudyService::clear()
{
    if (m_items.isEmpty()) {
        return;
    }
    m_items.clear();
    emit changed();
}

QString StudyService::itemBody(const StudyItem &item) const
{
    return item.isBlock() ? blockBody(item.id) : item.body;
}

bool StudyService::itemUsable(const StudyItem &item) const
{
    if (item.isBlock()) {
        return findBlock(item.id) != nullptr;
    }
    return !item.body.trimmed().isEmpty();
}

int StudyService::usableBlockCount() const
{
    int count = 0;
    for (const StudyItem &item : m_items) {
        if (item.isBlock() && findBlock(item.id) != nullptr) {
            ++count;
        }
    }
    return count;
}

QString StudyService::assemblePreview() const
{
    if (m_work == nullptr) {
        return {};
    }

    // 按**导入顺序**取功能块条目（源失效或正文缺失的不参与，绝不产出半成品脚本）
    std::vector<core::PythonBlock> blocks;
    std::vector<std::string> bodies;
    QStringList titles;
    for (const StudyItem &item : m_items) {
        if (!item.isBlock()) {
            continue; // 外部条目不参与拼接（没有 run_<id>() 入口）
        }
        const core::PythonBlock *block = findBlock(item.id);
        if (block == nullptr) {
            continue;
        }
        const QString body = m_work->blockBody(item.id);
        if (body.isEmpty()) {
            continue;
        }
        blocks.push_back(*block);
        bodies.push_back(toStd(body));
        titles.push_back(QString::fromStdString(block->title));
    }
    if (blocks.empty()) {
        return {};
    }

    const QDateTime now = QDateTime::currentDateTime();
    std::vector<std::string> header;
    header.push_back("WhalePet 工作台 · 学习页拼接预览（EX8；**只读**，不会被执行）");
    header.push_back(toStd(QStringLiteral("生成时间：")
                           + now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    header.push_back(toStd(QStringLiteral("功能块：") + titles.join(QStringLiteral("、"))));
    header.push_back("归位：所有顶层 import 语句已移到文件头（注释头之后）并去除重复");

    // 既有的组装口径（头注释 + 逐块原样 + 末尾分发器）→ import 归位与去重
    const std::string script = core::assembleScript(blocks, bodies, header, std::string());
    return QString::fromStdString(core::hoistImports(script));
}

QString StudyService::toJson() const
{
    return itemsToJson(m_items);
}

bool StudyService::loadFromJson(const QString &json)
{
    const QString text = json.trimmed();
    if (!text.isEmpty()) {
        // 非空但解析不了 / 不是数组：**保持现状**（不因一份坏文本静默清空用户的清单）；
        // 空文本才是「尚无清单」的默认值（清空）
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isArray()) {
            return false;
        }
    }

    int nextExternal = 1;
    const QVector<StudyItem> loaded = parseItemsJson(text, &nextExternal);
    if (itemsToJson(loaded) == itemsToJson(m_items)) {
        return false; // 与当前一致：不做任何改动（幂等，避免回声写库）
    }
    m_items = loaded;
    m_nextExternal = nextExternal;
    emit changed();
    return true;
}

} // namespace whalepet::viewmodel
