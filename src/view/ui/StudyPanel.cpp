#include "view/ui/StudyPanel.h"

#include "viewmodel/StudyService.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

namespace whalepet::ui {

namespace {

constexpr int kItemIdRole = Qt::UserRole + 1;
// 外部代码文本上限：既保护 settings.json_ext，也保护界面（超出即拒绝，不做截断）
constexpr qint64 kMaxExternalBytes = 256 * 1024;

QString itemLabel(const viewmodel::StudyItem &item)
{
    const QString category = item.category.isEmpty() ? QStringLiteral("条目") : item.category;
    QString label = QStringLiteral("[%1] %2").arg(category, item.title);
    if (!item.summary.isEmpty()) {
        label += QStringLiteral(" — ") + item.summary;
    }
    return label;
}

QString itemToolTip(const viewmodel::StudyItem &item)
{
    QStringList lines;
    lines.push_back(QStringLiteral("id：%1").arg(item.id));
    if (!item.requires.isEmpty()) {
        lines.push_back(QStringLiteral("依赖：%1").arg(item.requires));
    }
    if (item.isBlock()) {
        lines.push_back(QStringLiteral("来源：assets/python/blocks/%1.txt（干活功能块，原样）")
                            .arg(item.id));
    } else {
        lines.push_back(QStringLiteral("来源：手动导入的代码文本（原样保存）"));
    }
    return lines.join(QLatin1Char('\n'));
}

QString codeTextFor(const viewmodel::StudyItem &item, const QString &body)
{
    QStringList head;
    const QString category = item.category.isEmpty() ? QStringLiteral("条目") : item.category;
    head.push_back(QStringLiteral("# 条目：[%1] %2（%3）").arg(category, item.title, item.id));
    if (!item.requires.isEmpty()) {
        head.push_back(QStringLiteral("# 依赖：%1").arg(item.requires));
    }
    if (!item.summary.isEmpty()) {
        head.push_back(QStringLiteral("# 说明：%1").arg(item.summary));
    }
    head.push_back(item.isBlock()
                       ? QStringLiteral("# 来源：assets/python/blocks/%1.txt（干活功能块，原样）")
                             .arg(item.id)
                       : QStringLiteral("# 来源：手动导入的代码文本（原样保存，不会被改写）"));
    head.push_back(QString());

    QString text = head.join(QLatin1Char('\n'));
    if (body.isEmpty()) {
        text += QStringLiteral("# 代码块正文缺失：源已不在内容层索引里（可「移出选中」清理）\n");
    } else {
        text += body;
    }
    return text;
}

// 手动导入外部代码（标题 + 正文；可从文件载入）。返回 false = 取消 / 正文为空。
bool promptExternalCode(QWidget *parent, QString *title, QString *body)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("导入外部代码"));
    dialog.resize(640, 480);

    auto *layout = new QVBoxLayout(&dialog);
    auto *titleEdit = new QLineEdit(&dialog);
    titleEdit->setPlaceholderText(QStringLiteral("条目标题（可留空：自动命名为「外部代码 N」）"));
    layout->addWidget(titleEdit);

    auto *editor = new QPlainTextEdit(&dialog);
    editor->setPlaceholderText(QStringLiteral("把代码粘贴到这里……（只有读阅读用，不会被运行）"));
    layout->addWidget(editor, 1);

    auto *loadFromFile = new QPushButton(QStringLiteral("从文件载入…"), &dialog);
    layout->addWidget(loadFromFile);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        ok->setText(QStringLiteral("导入"));
    }
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setText(QStringLiteral("取消"));
    }
    layout->addWidget(buttons);

    QObject::connect(loadFromFile, &QPushButton::clicked, &dialog, [&dialog, editor, titleEdit] {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("选择代码文件"), QString(),
            QStringLiteral("代码 / 文本 (*.py *.pyw *.txt *.md);;所有文件 (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(&dialog, QStringLiteral("导入外部代码"),
                                 QStringLiteral("读不了这个文件：") + path);
            return;
        }
        const QByteArray bytes = file.read(kMaxExternalBytes + 1);
        if (bytes.size() > kMaxExternalBytes) {
            QMessageBox::warning(&dialog, QStringLiteral("导入外部代码"),
                                 QStringLiteral("文件太大（上限 256 KiB），请先裁剪后再导入"));
            return;
        }
        editor->setPlainText(QString::fromUtf8(bytes));
        if (titleEdit->text().trimmed().isEmpty()) {
            titleEdit->setText(QFileInfo(path).fileName());
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (editor->toPlainText().trimmed().isEmpty()) {
        return false;
    }
    if (title != nullptr) {
        *title = titleEdit->text();
    }
    if (body != nullptr) {
        *body = editor->toPlainText();
    }
    return true;
}

} // namespace

StudyPanel::StudyPanel(viewmodel::StudyService *service, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
{
    setObjectName(QStringLiteral("StudyPanel"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *hint = new QLabel(
        QStringLiteral("把「干活」里的功能块**手动导入**到这里只读学习（条目信息 + 原样代码块）；"
                       "也可以粘贴 / 从文件导入外部代码。\n"
                       "「拼接整脚本」按导入顺序拼出完整脚本，并把所有顶层 import 归位到文件头、去除重复；"
                       "**只读预览，不会被执行**。"),
        this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *lists = new QHBoxLayout();
    lists->setSpacing(8);

    auto *left = new QVBoxLayout();
    left->addWidget(new QLabel(QStringLiteral("可以导入的「干活」条目"), this));
    m_available = new QListWidget(this);
    m_available->setObjectName(QStringLiteral("StudyAvailableList"));
    m_available->setSelectionMode(QAbstractItemView::ExtendedSelection);
    left->addWidget(m_available, 1);
    auto *leftButtons = new QHBoxLayout();
    m_importSelected = new QPushButton(QStringLiteral("导入选中"), this);
    m_importSelected->setObjectName(QStringLiteral("StudyImportSelected"));
    m_importAll = new QPushButton(QStringLiteral("导入全部"), this);
    m_importAll->setObjectName(QStringLiteral("StudyImportAll"));
    leftButtons->addWidget(m_importSelected);
    leftButtons->addWidget(m_importAll);
    leftButtons->addStretch();
    left->addLayout(leftButtons);
    lists->addLayout(left, 1);

    auto *right = new QVBoxLayout();
    right->addWidget(new QLabel(QStringLiteral("已导入的学习条目"), this));
    m_imported = new QListWidget(this);
    m_imported->setObjectName(QStringLiteral("StudyImportedList"));
    m_imported->setSelectionMode(QAbstractItemView::ExtendedSelection);
    right->addWidget(m_imported, 1);
    auto *rightButtons = new QHBoxLayout();
    m_removeSelected = new QPushButton(QStringLiteral("移出选中"), this);
    m_removeSelected->setObjectName(QStringLiteral("StudyRemoveSelected"));
    m_clear = new QPushButton(QStringLiteral("清空"), this);
    m_clear->setObjectName(QStringLiteral("StudyClearAll"));
    m_importExternal = new QPushButton(QStringLiteral("导入外部代码…"), this);
    m_importExternal->setObjectName(QStringLiteral("StudyImportExternal"));
    rightButtons->addWidget(m_removeSelected);
    rightButtons->addWidget(m_clear);
    rightButtons->addWidget(m_importExternal);
    rightButtons->addStretch();
    right->addLayout(rightButtons);
    lists->addLayout(right, 1);

    root->addLayout(lists, 1);

    auto *previewRow = new QHBoxLayout();
    m_preview = new QPushButton(QStringLiteral("拼接整脚本（import 归位去重）"), this);
    m_preview->setObjectName(QStringLiteral("StudyPreviewScript"));
    previewRow->addWidget(m_preview);
    previewRow->addStretch();
    root->addLayout(previewRow);

    m_code = new QPlainTextEdit(this);
    m_code->setObjectName(QStringLiteral("StudyCodeView"));
    m_code->setReadOnly(true);
    m_code->setPlaceholderText(
        QStringLiteral("在这里读条目信息与代码块正文（选中右侧条目 / 点「拼接整脚本」）"));
    root->addWidget(m_code, 2);

    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("StudyMessage"));
    m_message->setWordWrap(true);
    root->addWidget(m_message);

    connect(m_importSelected, &QPushButton::clicked, this, &StudyPanel::onImportSelected);
    connect(m_importAll, &QPushButton::clicked, this, &StudyPanel::onImportAll);
    connect(m_removeSelected, &QPushButton::clicked, this, &StudyPanel::onRemoveSelected);
    connect(m_clear, &QPushButton::clicked, this, &StudyPanel::onClearAll);
    connect(m_importExternal, &QPushButton::clicked, this, &StudyPanel::onImportExternal);
    connect(m_preview, &QPushButton::clicked, this, &StudyPanel::onPreview);
    connect(m_imported, &QListWidget::currentRowChanged, this, [this](int row) {
        const QListWidgetItem *item = m_imported->item(row);
        showItemCode((item != nullptr) ? item->data(kItemIdRole).toString() : QString());
    });

    if (m_service != nullptr) {
        connect(m_service, &viewmodel::StudyService::changed, this, &StudyPanel::refresh);
    }

    reloadAvailable();
    reloadImported();
    if (m_service != nullptr && m_service->items().isEmpty()) {
        m_message->setText(QStringLiteral("还没有导入任何条目：在左侧选中功能块后点「导入选中」。"));
    }
}

void StudyPanel::refresh()
{
    reloadAvailable();
    reloadImported();

    // 代码视图所显示的条目被移出时，清空代码区并说明（避免展示已消失的条目）
    if (!m_shownId.isEmpty() && (m_service == nullptr || !m_service->contains(m_shownId))) {
        m_shownId.clear();
        m_code->clear();
    }
}

void StudyPanel::reloadAvailable()
{
    // 保持候选区的选中项（导入多条时不必重新点选）
    QStringList selected;
    for (int i = 0; i < m_available->count(); ++i) {
        const QListWidgetItem *item = m_available->item(i);
        if (item->isSelected()) {
            selected.push_back(item->data(kItemIdRole).toString());
        }
    }

    m_available->clear();
    if (m_service == nullptr) {
        return;
    }
    for (const core::PythonBlock &block : m_service->availableBlocks()) {
        const QString id = QString::fromStdString(block.id);
        const bool imported = m_service->contains(id);
        viewmodel::StudyItem preview;
        preview.id = id;
        preview.kind = QStringLiteral("block");
        preview.title = QString::fromStdString(block.title);
        preview.category = QString::fromStdString(block.category);
        preview.summary = QString::fromStdString(block.summary);
        preview.requires = QString::fromStdString(block.requires);

        auto *item = new QListWidgetItem(
            itemLabel(preview) + (imported ? QStringLiteral("（已导入）") : QString()), m_available);
        item->setData(kItemIdRole, id);
        item->setToolTip(itemToolTip(preview));
        if (selected.contains(id)) {
            item->setSelected(true);
        }
    }
    if (m_available->count() == 0) {
        m_message->setText(QStringLiteral("没有加载到「干活」功能块（assets/python/blocks 资源缺失）。"));
    }
}

void StudyPanel::reloadImported()
{
    m_imported->clear();
    if (m_service == nullptr) {
        return;
    }
    for (const viewmodel::StudyItem &item : m_service->items()) {
        auto *row = new QListWidgetItem(itemLabel(item), m_imported);
        row->setData(kItemIdRole, item.id);
        row->setToolTip(itemToolTip(item));
        if (m_shownId == item.id) {
            row->setSelected(true);
            m_imported->setCurrentItem(row);
        }
    }
}

void StudyPanel::showItemCode(const QString &itemId)
{
    if (m_service == nullptr || itemId.isEmpty()) {
        return;
    }
    for (const viewmodel::StudyItem &item : m_service->items()) {
        if (item.id != itemId) {
            continue;
        }
        m_shownId = item.id;
        m_code->setPlainText(codeTextFor(item, m_service->itemBody(item)));
        return;
    }
}

void StudyPanel::onImportSelected()
{
    if (m_service == nullptr) {
        return;
    }
    QStringList ids;
    for (const QListWidgetItem *item : m_available->selectedItems()) {
        ids.push_back(item->data(kItemIdRole).toString());
    }
    if (ids.isEmpty()) {
        m_message->setText(QStringLiteral("先在左侧选中要导入的条目吧。"));
        return;
    }
    const int added = m_service->importBlocks(ids);
    // refresh 由 service::changed 触发；这里只补提示与「选中最新的那条看代码」
    m_message->setText(QStringLiteral("已导入 %1 条（未知 id / 重复导入会跳过）。").arg(added));
    if (added > 0) {
        const QString latest = ids.last();
        showItemCode(latest);
        for (int i = 0; i < m_imported->count(); ++i) {
            if (m_imported->item(i)->data(kItemIdRole).toString() == latest) {
                m_imported->setCurrentRow(i);
                break;
            }
        }
    }
}

void StudyPanel::onImportAll()
{
    if (m_service == nullptr) {
        return;
    }
    QStringList ids;
    for (int i = 0; i < m_available->count(); ++i) {
        ids.push_back(m_available->item(i)->data(kItemIdRole).toString());
    }
    const int added = m_service->importBlocks(ids);
    m_message->setText(QStringLiteral("已导入 %1 条（已导入的会跳过）。").arg(added));
}

void StudyPanel::onRemoveSelected()
{
    if (m_service == nullptr) {
        return;
    }
    QStringList ids;
    for (const QListWidgetItem *item : m_imported->selectedItems()) {
        ids.push_back(item->data(kItemIdRole).toString());
    }
    if (ids.isEmpty()) {
        m_message->setText(QStringLiteral("先在右侧选中要移出的条目吧。"));
        return;
    }
    const int removed = m_service->removeItems(ids);
    m_message->setText(QStringLiteral("已移出 %1 条。").arg(removed));
}

void StudyPanel::onClearAll()
{
    if (m_service == nullptr || m_service->items().isEmpty()) {
        m_message->setText(QStringLiteral("清单已经是空的。"));
        return;
    }
    const auto answer = QMessageBox::question(
        this, QStringLiteral("清空学习条目"),
        QStringLiteral("要移出全部已导入的学习条目吗？（外部代码条目会一并丢失）"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_service->clear();
    m_message->setText(QStringLiteral("已清空学习条目。"));
}

void StudyPanel::onImportExternal()
{
    if (m_service == nullptr) {
        return;
    }
    QString title;
    QString body;
    if (!promptExternalCode(this, &title, &body)) {
        return;
    }
    const QString id = m_service->addExternal(title, body);
    if (id.isEmpty()) {
        m_message->setText(QStringLiteral("外部代码正文为空，已放弃导入。"));
        return;
    }
    m_message->setText(QStringLiteral("已导入外部代码条目：") + id);
    showItemCode(id);
    for (int i = 0; i < m_imported->count(); ++i) {
        if (m_imported->item(i)->data(kItemIdRole).toString() == id) {
            m_imported->setCurrentRow(i);
            break;
        }
    }
}

void StudyPanel::onPreview()
{
    if (m_service == nullptr) {
        return;
    }
    const QString script = m_service->assemblePreview();
    if (script.isEmpty()) {
        m_code->clear();
        m_shownId.clear();
        m_message->setText(QStringLiteral("还没有可拼接的功能块条目"
                                          "（外部代码条目不参与拼接：它们没有 run_<id>() 入口）。"));
        return;
    }
    m_shownId.clear();
    m_code->setPlainText(script);
    m_message->setText(QStringLiteral("已拼接 %1 个功能块：顶层 import 已归位到文件头并去除重复。")
                           .arg(m_service->usableBlockCount()));
}

} // namespace whalepet::ui
