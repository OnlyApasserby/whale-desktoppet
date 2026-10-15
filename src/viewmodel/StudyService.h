#pragma once

// EX8 学习页（工作台「学习」栏）的条目模型与编排 —— docs/STUDY.md。
//
// 职责：
//   * 持有「已导入的学习条目」**有序清单**（干活功能块条目 + 手动导入的外部代码条目）；
//   * 功能块条目**只存 id**：标题 / 类别 / 说明 / 依赖与代码块正文都实时取自干活内容层
//     （`WorkService` 的唯一索引与 `<id>.txt`），内容层更新后学习页随之更新；
//   * 拼接预览：按**导入顺序**取功能块条目 → `core::assembleScript`（既有组装口径）
//     → `core::hoistImports`（顶层 import 归位到文件头并去重，见 core/PythonImports.h）；
//   * 持久化：清单以 JSON 文本交给宿主（设置键 `study_imported`，json_ext 风格），
//     本类**不直接读写数据库**（与其它服务同口径，写库由宿主的 applySettings 链路负责）。
//
// 边界：
//   * 学习页**只读**：不执行、不联网、不写文件（外部代码文本原样保存，仅用于阅读）；
//   * 外部条目不参与拼接（没有 `run_<id>()` 入口，进分发器会产出跑不通的脚本）。

#include "core/PythonBlocks.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace whalepet::viewmodel {

class WorkService;

// 一条学习条目。kind == "block" 时只有 id 有持久化意义（其余字段由内容层实时解析）；
// kind == "external" 时 title / body 来自用户手动导入并随清单一起持久化。
struct StudyItem {
    QString id;       // 功能块 id，或外部条目的生成 id（"external:<n>"）
    QString kind;     // "block" | "external"
    QString title;    // 条目标题
    QString category; // 类别（功能块来自索引；外部条目为「外部」）
    QString summary;  // 一句话说明
    QString requires; // 依赖说明（仅功能块条目）
    QString body;     // 仅外部条目携带正文；功能块正文实时读源

    bool isBlock() const { return kind == QStringLiteral("block"); }
    bool isExternal() const { return kind == QStringLiteral("external"); }
};

class StudyService : public QObject {
    Q_OBJECT
public:
    explicit StudyService(QObject *parent = nullptr);

    // 干活内容层的只读入口（非拥有；为空 = 只支持外部代码条目）
    void setWorkService(WorkService *work);
    WorkService *workService() const { return m_work; }

    // ---- 可导入的「干活」条目 ----
    // 内容层唯一索引的全部条目（顺序即索引顺序）；学习页据此列出候选
    QVector<core::PythonBlock> availableBlocks() const;
    // 按 id 读代码块正文（非法 id / 源缺失返回空串）
    QString blockBody(const QString &blockId) const;

    // ---- 已导入清单 ----
    // 解析后的清单（功能块条目的元数据按**当前**索引解析；源缺失时标记为「已失效」）
    QVector<StudyItem> items() const;
    bool contains(const QString &id) const;
    // 手动导入功能块条目：未知 id / 已导入的跳过（幂等）；返回新增条数
    int importBlocks(const QStringList &blockIds);
    // 手动导入外部代码文本：正文为空则拒绝（返回空串）；返回新条目 id
    QString addExternal(const QString &title, const QString &body);
    // 移出条目；返回移除条数
    int removeItems(const QStringList &ids);
    void clear();

    // ---- 只读阅读 ----
    // 条目当前可读的代码（功能块实时读源）
    QString itemBody(const StudyItem &item) const;
    // 条目是否仍可用（功能块必须在唯一索引里；外部条目正文非空）
    bool itemUsable(const StudyItem &item) const;
    // 可参与拼接的功能块条目数
    int usableBlockCount() const;

    // ---- 拼接预览（**功能块条目**按导入顺序；无可用功能块返回空串）----
    QString assemblePreview() const;

    // ---- 持久化（study_imported，JSON 数组文本）----
    QString toJson() const;
    // 载入清单；与当前一致时不做任何改动并返回 false（避免回声写库）
    bool loadFromJson(const QString &json);

signals:
    void changed(); // 清单发生变化（面板刷新；插件据此回写宿主）

private:
    const core::PythonBlock *findBlock(const QString &id) const;

    WorkService *m_work = nullptr; // 非拥有
    QVector<StudyItem> m_items;    // 已导入清单（有序；功能块只存 id）
    int m_nextExternal = 1;        // 外部条目序号（"external:<n>"）
};

} // namespace whalepet::viewmodel
