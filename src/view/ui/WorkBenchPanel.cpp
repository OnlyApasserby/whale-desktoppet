#include "view/ui/WorkBenchPanel.h"

#include "viewmodel/WorkService.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace whalepet::ui {

namespace {
constexpr int kBlockIdRole = Qt::UserRole + 1;

QString formatClock(int seconds)
{
    const int safe = (seconds < 0) ? 0 : seconds;
    return QStringLiteral("%1:%2")
        .arg(safe / 60, 2, 10, QLatin1Char('0'))
        .arg(safe % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

WorkBenchPanel::WorkBenchPanel(viewmodel::WorkService *service, FillPagesFn fillPages, QWidget *parent)
    : QWidget(parent, Qt::Tool)
    , m_service(service)
{
    setWindowTitle(QStringLiteral("工作台"));
    resize(560, 640);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // P11：面板即**页签容器** —— 第 1 页是 P10 的「干活」页，其余页由插件经
    // WorkbenchPage 贡献点追加（页的标题 / 顺序 / 内容由插件决定，本类不认识它们）。
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("WorkBenchTabs"));
    root->addWidget(m_tabs, 1);

    m_workPage = new QWidget(m_tabs);
    auto *page = new QVBoxLayout(m_workPage);
    page->setContentsMargins(8, 8, 8, 8);
    page->setSpacing(8);
    m_tabs->addTab(m_workPage, QStringLiteral("干活"));

    auto *hint = new QLabel(
        QStringLiteral("勾选想让鲸鱼娘做的事，她会按所选功能块拼出一个 Python 脚本；"
                       "**脚本不会被自动运行**，请自行检查后再用。"),
        m_workPage);
    hint->setWordWrap(true);
    page->addWidget(hint);

    m_blocks = new QListWidget(m_workPage);
    m_blocks->setSelectionMode(QAbstractItemView::NoSelection);
    page->addWidget(m_blocks, 1);

    auto *durationRow = new QHBoxLayout();
    durationRow->addWidget(new QLabel(QStringLiteral("工作时长"), m_workPage));
    m_duration = new QComboBox(m_workPage);
    for (const int minutes : viewmodel::WorkService::durationPresets()) {
        m_duration->addItem(QString::number(minutes) + QStringLiteral(" 分钟"), minutes);
    }
    durationRow->addWidget(m_duration);
    durationRow->addStretch();
    page->addLayout(durationRow);

    auto *dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(QStringLiteral("产出目录"), m_workPage));
    m_outputDir = new QLineEdit(m_workPage);
    m_outputDir->setPlaceholderText(QStringLiteral("脚本输出目录（必填；不会被自动猜测）"));
    dirRow->addWidget(m_outputDir, 1);
    auto *browse = new QPushButton(QStringLiteral("浏览…"), m_workPage);
    dirRow->addWidget(browse);
    page->addLayout(dirRow);

    auto *buttonRow = new QHBoxLayout();
    m_start = new QPushButton(QStringLiteral("开始工作"), m_workPage);
    m_produceNow = new QPushButton(QStringLiteral("立即产出"), m_workPage);
    m_cancel = new QPushButton(QStringLiteral("取消"), m_workPage);
    m_openDir = new QPushButton(QStringLiteral("打开目录"), m_workPage);
    buttonRow->addWidget(m_start);
    buttonRow->addWidget(m_produceNow);
    buttonRow->addWidget(m_cancel);
    buttonRow->addStretch();
    buttonRow->addWidget(m_openDir);
    page->addLayout(buttonRow);

    m_progress = new QProgressBar(m_workPage);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    page->addWidget(m_progress);

    m_status = new QLabel(m_workPage);
    m_status->setWordWrap(true);
    page->addWidget(m_status);

    connect(browse, &QPushButton::clicked, this, &WorkBenchPanel::onBrowseDir);
    connect(m_start, &QPushButton::clicked, this, &WorkBenchPanel::onStart);
    connect(m_produceNow, &QPushButton::clicked, this, &WorkBenchPanel::onProduceNow);
    connect(m_cancel, &QPushButton::clicked, this, &WorkBenchPanel::onCancel);
    connect(m_openDir, &QPushButton::clicked, this, [this] {
        QString dir = m_outputDir->text().trimmed();
        if (dir.isEmpty() && m_service != nullptr) {
            dir = m_service->outputDir();
        }
        if (!dir.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        }
    });
    connect(m_outputDir, &QLineEdit::editingFinished, this, [this] { applyOutputDir(m_outputDir->text()); });
    connect(m_duration, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_service != nullptr) {
            m_service->setDurationMinutes(m_duration->currentData().toInt());
        }
    });

    if (m_service != nullptr) {
        connect(m_service, &viewmodel::WorkService::stateChanged, this, &WorkBenchPanel::refreshState);
        connect(m_service, &viewmodel::WorkService::tick, this, [this](int elapsed, int total) {
            const int percent = (total > 0) ? (elapsed * 100 / total) : 0;
            m_progress->setValue(percent);
            QString text = QStringLiteral("工作中 ") + formatClock(elapsed) + QStringLiteral(" / ")
                           + formatClock(total);
            if (m_service->eggArmed()) {
                text += QStringLiteral("（已经准备好一句悄悄话）");
            }
            m_status->setText(text);
        });
        connect(m_service, &viewmodel::WorkService::finished, this,
                [this](const QString &path, bool withEgg) {
                    m_progress->setValue(100);
                    QString text = QStringLiteral("写好了：") + path;
                    if (withEgg) {
                        text += QStringLiteral("（里面藏了一句俏皮话）");
                    }
                    m_status->setText(text);
                    refreshState();
                });
        connect(m_service, &viewmodel::WorkService::failed, this, [this](const QString &reason) {
            m_progress->setValue(0);
            m_status->setText(QStringLiteral("没能开工：") + reason);
            refreshState();
        });

        m_outputDir->setText(m_service->outputDir());
        const int index = m_duration->findData(m_service->durationMinutes());
        if (index >= 0) {
            m_duration->setCurrentIndex(index);
        }
    }

    reloadBlocks();
    refreshState();

    // P11：追加插件贡献的工作台页（宿主注入的通用分发；空 = 只有「干活」页）。
    // 放在最后：页面的创建不受 P10 控件状态影响，且宿主分发失败不影响干活页可用。
    if (fillPages) {
        fillPages(m_tabs);
    }
}

QStringList WorkBenchPanel::checkedBlockIds() const
{
    QStringList ids;
    for (int i = 0; i < m_blocks->count(); ++i) {
        const QListWidgetItem *item = m_blocks->item(i);
        if (item->checkState() == Qt::Checked) {
            ids.push_back(item->data(kBlockIdRole).toString());
        }
    }
    return ids;
}

void WorkBenchPanel::reloadBlocks()
{
    m_blocks->clear();
    if (m_service == nullptr) {
        return;
    }
    for (const core::PythonBlock &block : m_service->blocks()) {
        const QString id = QString::fromStdString(block.id);
        auto *item = new QListWidgetItem(
            QStringLiteral("[%1] %2 — %3")
                .arg(QString::fromStdString(block.category),
                     QString::fromStdString(block.title),
                     QString::fromStdString(block.summary)),
            m_blocks);
        item->setData(kBlockIdRole, id);
        item->setToolTip(QStringLiteral("用法：python <脚本> %1 [参数…]\n依赖：%2")
                             .arg(id, QString::fromStdString(block.requires)));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    if (m_blocks->count() == 0) {
        m_status->setText(QStringLiteral("没有加载到功能块（assets/python/blocks 资源缺失）"));
    } else if (m_status->text().isEmpty()) {
        m_status->setText(QStringLiteral("选好功能块与目录，点「开始工作」；也可以直接「立即产出」。"));
    }
}

void WorkBenchPanel::refreshState()
{
    const bool running = (m_service != nullptr) && m_service->running();
    m_start->setEnabled(!running);
    m_produceNow->setEnabled(running);
    m_cancel->setEnabled(running);
    m_blocks->setEnabled(!running);
    m_duration->setEnabled(!running);
    m_outputDir->setEnabled(!running);
    if (!running && m_progress->value() == 100 && m_status->text().startsWith(QStringLiteral("写好了"))) {
        return; // 产出成功后的状态文案保留到下次开工
    }
    if (!running) {
        m_progress->setValue(0);
    }
}

void WorkBenchPanel::applyOutputDir(const QString &dir)
{
    const QString trimmed = dir.trimmed();
    if (m_service != nullptr) {
        m_service->setOutputDir(trimmed);
    }
    if (outputDirChosen) {
        outputDirChosen(trimmed);
    }
}

void WorkBenchPanel::onBrowseDir()
{
    const QString start = m_outputDir->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择脚本产出目录"), start);
    if (dir.isEmpty()) {
        return;
    }
    m_outputDir->setText(dir);
    applyOutputDir(dir);
}

void WorkBenchPanel::onStart()
{
    if (m_service == nullptr) {
        return;
    }
    const QStringList ids = checkedBlockIds();
    if (ids.isEmpty()) {
        m_status->setText(QStringLiteral("至少勾选一个功能块再开工哦。"));
        return;
    }
    applyOutputDir(m_outputDir->text());
    if (!m_service->start(ids)) {
        return; // 失败理由经 failed 信号显示
    }
    m_progress->setValue(0);
    m_status->setText(QStringLiteral("开工啦，鲸鱼娘正在写……"));
    refreshState();
}

void WorkBenchPanel::onProduceNow()
{
    if (m_service != nullptr) {
        m_service->produceNow();
    }
}

void WorkBenchPanel::onCancel()
{
    if (m_service != nullptr) {
        m_service->cancel();
    }
}

} // namespace whalepet::ui
