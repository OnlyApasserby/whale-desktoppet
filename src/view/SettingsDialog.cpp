#include "view/SettingsDialog.h"

#include "minigame/MiniGameRegistry.h"
#include "model/Database.h"
#include "model/SettingsRepo.h"
#include "view/ContentPanel.h"
#include "view/PoseView.h"

#include <QCheckBox>
#include <QDebug>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace whalepet {

SettingsDialog::SettingsDialog(model::Database *db, viewmodel::AchievementService *achievement,
                               viewmodel::QuestService *quest, viewmodel::SigninService *signin,
                               MiniGameRegistry *miniGames, QWidget *parent)
    : QDialog(parent)
    , m_db(db)
    , m_miniGames(miniGames)
{
    // 构造期间构建控件会触发 toggled/valueChanged，先抑制持久化，末尾 reload() 统一放开
    m_loading = true;

    setWindowTitle(QStringLiteral("鲸鱼娘 · 设置"));
    setModal(false);
    setMinimumSize(460, 520);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildAppearanceTab(), QStringLiteral("陪伴表现"));

    // 日常 / 成就墙 / 成长日记：复用 ContentPanel 的三个页面（内嵌进本面板）
    m_content = new ContentPanel(achievement, quest, signin, db, this);
    connect(m_content, &ContentPanel::signInRequested, this, &SettingsDialog::signInRequested);
    connect(m_content, &ContentPanel::questClaimRequested, this,
            &SettingsDialog::questClaimRequested);
    m_content->embedInto(tabs);

    tabs->addTab(buildMiniGameTab(), QStringLiteral("小游戏"));
    // P9-B：外部进程插件（MCP Server）只读状态展示
    tabs->addTab(buildProcessPluginTab(), QStringLiteral("外部插件"));
    tabs->addTab(buildDataTab(), QStringLiteral("数据与重置"));

    auto *root = new QVBoxLayout(this);
    root->addWidget(tabs);

    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    root->addWidget(close);

    reload();
}

QWidget *SettingsDialog::buildAppearanceTab()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);

    auto *box = new QGroupBox(QStringLiteral("陪伴表现"), page);
    auto *form = new QFormLayout(box);

    const auto addToggle = [this, box, form](const QString &text, QCheckBox *&slot) {
        auto *cb = new QCheckBox(text, box);
        connect(cb, &QCheckBox::toggled, this, [this](bool) { persist(); });
        form->addRow(cb);
        slot = cb;
    };

    addToggle(QStringLiteral("显示桌宠"), m_petEnabled);
    addToggle(QStringLiteral("台词气泡"), m_bubbleEnabled);
    addToggle(QStringLiteral("粒子 / 特效"), m_particlesEnabled);
    addToggle(QStringLiteral("关键词感知（梗表情）"), m_keywordAware);
    addToggle(QStringLiteral("深夜静默（23:00–05:59 不主动发言）"), m_nightQuiet);
    addToggle(QStringLiteral("拖拽惯性"), m_dragInertia);
    addToggle(QStringLiteral("回收站清理提醒（sweep）"), m_recycleBinReminder);

    m_poseSize = new QSpinBox(box);
    m_poseSize->setRange(PoseView::kMinDisplaySize, PoseView::kMaxDisplaySize);
    m_poseSize->setSingleStep(20);
    m_poseSize->setSuffix(QStringLiteral(" px"));
    connect(m_poseSize, &QSpinBox::valueChanged, this, [this](int) { persist(); });
    form->addRow(QStringLiteral("立绘尺寸"), m_poseSize);

    outer->addWidget(box);

    // ---- P8：预设对话 + 彩云天气 ----
    auto *talkBox = new QGroupBox(QStringLiteral("预设对话"), page);
    auto *talkForm = new QFormLayout(talkBox);

    m_dialogueEnabled = new QCheckBox(QStringLiteral("陪我聊聊（低频主动提问）"), talkBox);
    connect(m_dialogueEnabled, &QCheckBox::toggled, this, [this](bool) { persist(); });
    talkForm->addRow(m_dialogueEnabled);

    m_weatherKey = new QLineEdit(talkBox);
    m_weatherKey->setPlaceholderText(QStringLiteral("彩云天气 API key（留空 = 不联网）"));
    connect(m_weatherKey, &QLineEdit::editingFinished, this, [this]() { persist(); });
    talkForm->addRow(QStringLiteral("天气 key"), m_weatherKey);

    m_weatherLocation = new QLineEdit(talkBox);
    m_weatherLocation->setPlaceholderText(QStringLiteral("城市名（如 上海）或经纬度（如 116.23,39.93）"));
    connect(m_weatherLocation, &QLineEdit::editingFinished, this, [this]() { persist(); });
    talkForm->addRow(QStringLiteral("天气城市"), m_weatherLocation);

    outer->addWidget(talkBox);

    // ---- EX 彩蛋：代码彩蛋（戳一戳 → 低概率在源码注释里藏俏皮话） ----
    auto *eggBox = new QGroupBox(QStringLiteral("代码彩蛋"), page);
    auto *eggForm = new QFormLayout(eggBox);

    m_codeEggEnabled = new QCheckBox(
        QStringLiteral("戳一戳时，5% 概率在源码注释里藏一句俏皮话"), eggBox);
    connect(m_codeEggEnabled, &QCheckBox::toggled, this, [this](bool) { persist(); });
    eggForm->addRow(m_codeEggEnabled);

    auto *eggRow = new QWidget(eggBox);
    auto *eggRowLayout = new QHBoxLayout(eggRow);
    eggRowLayout->setContentsMargins(0, 0, 0, 0);
    eggRowLayout->setSpacing(6);
    m_codeEggWorkspace = new QLineEdit(eggRow);
    m_codeEggWorkspace->setPlaceholderText(QStringLiteral("工作区目录（留空 = 不动作）"));
    connect(m_codeEggWorkspace, &QLineEdit::editingFinished, this, [this]() { persist(); });
    eggRowLayout->addWidget(m_codeEggWorkspace);
    auto *eggBrowse = new QPushButton(QStringLiteral("浏览…"), eggRow);
    connect(eggBrowse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择代码彩蛋工作区"), m_codeEggWorkspace->text());
        if (!dir.isEmpty()) {
            m_codeEggWorkspace->setText(dir);
            persist();
        }
    });
    eggRowLayout->addWidget(eggBrowse);
    eggForm->addRow(QStringLiteral("工作区"), eggRow);

    outer->addWidget(eggBox);

    auto *hint = new QLabel(
        QStringLiteral("关键词感知默认关闭；开启后仅在你主动录入热词 / 复制文本时匹配梗词。\n"
                       "「显示桌宠」关闭后窗口隐藏，可通过托盘或左下角入口唤回。\n"
                       "预设对话：只在静息（非工作 / 非深夜 / 气泡空闲）时低频提问，回答后自动刷新问题池；"
                       "天气问题按彩云天气类型作答，**key 与城市都填写后才会联网**。\n"
                       "代码彩蛋（默认关闭）：开启后对鲸鱼娘「戳一戳」时有 5% 概率在「工作区」的 "
                       ".py / .c / .cpp / .h 文件注释里追加一句俏皮话；**只加注释、不改代码、同文件只藏一次**"
                       "（含 whalepet-egg 标记）。工作区留空则完全不动作。"),
        page);
    hint->setWordWrap(true);
    outer->addWidget(hint);
    outer->addStretch();
    return page;
}

QWidget *SettingsDialog::buildMiniGameTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    m_minigameEnabled = new QCheckBox(QStringLiteral("启用小游戏"), page);
    connect(m_minigameEnabled, &QCheckBox::toggled, this, [this](bool) { persist(); });
    layout->addWidget(m_minigameEnabled);

    // 按已注册插件动态生成：每个插件一组（名称 / 上次配置 / 玩法说明 / 开始）
    const int pluginCount = (m_miniGames != nullptr) ? m_miniGames->count() : 0;
    if (pluginCount == 0) {
        auto *empty = new QLabel(QStringLiteral("当前没有已注册的小游戏插件。"), page);
        empty->setWordWrap(true);
        layout->addWidget(empty);
    }
    for (int i = 0; i < pluginCount; ++i) {
        IMiniGamePlugin *plugin = m_miniGames->at(i);
        if (plugin == nullptr) {
            continue;
        }
        const MiniGameInfo info = plugin->info();

        auto *box = new QGroupBox(info.displayName, page);
        auto *boxLayout = new QVBoxLayout(box);

        auto *configLabel = new QLabel(box);
        configLabel->setWordWrap(true);
        boxLayout->addWidget(configLabel);
        m_miniGameConfigLabels.insert(info.id, configLabel);

        auto *description = new QLabel(info.description, box);
        description->setWordWrap(true);
        boxLayout->addWidget(description);

        auto *start = new QPushButton(QStringLiteral("开始%1").arg(info.displayName), box);
        const QString id = info.id;
        connect(start, &QPushButton::clicked, this,
                [this, id] { emit openMiniGameRequested(id); });
        boxLayout->addWidget(start);

        layout->addWidget(box);
    }

    auto *hint = new QLabel(
        QStringLiteral("小游戏按插件接入：关闭本开关后，右键 / 托盘菜单不再显示任何游戏入口。"),
        page);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::buildProcessPluginTab()
{
    // P9-B：外部进程插件（MCP Server）的**只读**状态列表。
    // 数据由 PetWindow 在打开面板前经 setProcessPluginStatuses() 注入（不触发落库）。
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    m_processTable = new QTableWidget(0, 5, page);
    m_processTable->setHorizontalHeaderLabels({ QStringLiteral("插件"), QStringLiteral("程序"),
                                               QStringLiteral("状态"), QStringLiteral("工具数"),
                                               QStringLiteral("说明") });
    m_processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_processTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_processTable->verticalHeader()->setVisible(false);
    m_processTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_processTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_processTable);

    auto *hint = new QLabel(
        QStringLiteral("外部进程插件由数据目录下的 plugins.json 配置，以独立进程运行（崩溃隔离），"
                       "能力 id 前缀为 ext.<插件id>.。本页只读展示运行状态；"
                       "新增 / 修改插件请编辑该文件后重启桌宠。"),
        page);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch();
    return page;
}

void SettingsDialog::setProcessPluginStatuses(const QList<plugin::ProcessPluginStatus> &statuses)
{
    m_processStatuses = statuses;
    if (m_processTable == nullptr) {
        return;
    }

    if (m_processStatuses.isEmpty()) {
        m_processTable->setRowCount(1);
        m_processTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("（未配置外部插件）")));
        for (int column = 1; column < 5; ++column) {
            m_processTable->setItem(0, column, new QTableWidgetItem(QString()));
        }
        return;
    }

    m_processTable->setRowCount(m_processStatuses.size());
    for (int row = 0; row < m_processStatuses.size(); ++row) {
        const plugin::ProcessPluginStatus &status = m_processStatuses.at(row);
        QString state = QStringLiteral("未运行");
        if (!status.valid) {
            state = QStringLiteral("配置非法");
        } else if (status.running) {
            state = QStringLiteral("运行中");
        }
        const QString note = status.reason.isEmpty() ? QStringLiteral("正常") : status.reason;

        m_processTable->setItem(row, 0, new QTableWidgetItem(status.pluginId));
        m_processTable->setItem(row, 1, new QTableWidgetItem(status.program));
        m_processTable->setItem(row, 2, new QTableWidgetItem(state));
        m_processTable->setItem(row, 3, new QTableWidgetItem(QString::number(status.toolCount)));
        m_processTable->setItem(row, 4, new QTableWidgetItem(note));
    }
}

QWidget *SettingsDialog::buildDataTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *resetPos = new QPushButton(QStringLiteral("重置位置（回到屏幕中央）"), page);
    connect(resetPos, &QPushButton::clicked, this, &SettingsDialog::resetPositionRequested);
    layout->addWidget(resetPos);

    auto *openDir = new QPushButton(QStringLiteral("打开数据目录"), page);
    connect(openDir, &QPushButton::clicked, this, &SettingsDialog::openDataDirRequested);
    layout->addWidget(openDir);

    layout->addSpacing(12);

    auto *resetGrowth = new QPushButton(QStringLiteral("重置养成数据…"), page);
    connect(resetGrowth, &QPushButton::clicked, this, [this] {
        const auto ret = QMessageBox::warning(
            this, QStringLiteral("重置养成数据"),
            QStringLiteral("将把等级 / 经验 / 心情 / 好感 / 饱食 / 羁绊 / 陪伴时长 / 连续签到\n"
                           "全部重置为初始值。\n\n"
                           "成就墙、每日任务与成长日记作为历史记录保留。\n\n"
                           "确定要重置吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            emit resetGrowthRequested();
        }
    });
    layout->addWidget(resetGrowth);

    auto *hint = new QLabel(
        QStringLiteral("「重置位置」把桌宠移回当前主屏正中央（每次启动也会自动居中）；"
                       "「重置养成数据」需二次确认，不可撤销。"),
        page);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch();
    return page;
}

void SettingsDialog::reload()
{
    model::SettingsData data; // 默认值起底；无记录 / 读失败时即用默认
    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        repo.load(data);
    }

    m_loading = true;
    m_petEnabled->setChecked(data.petEnabled);
    m_bubbleEnabled->setChecked(data.bubbleEnabled);
    m_particlesEnabled->setChecked(data.particlesEnabled);
    m_keywordAware->setChecked(data.keywordAware);
    m_nightQuiet->setChecked(data.nightQuiet);
    m_dragInertia->setChecked(data.dragInertia);
    m_recycleBinReminder->setChecked(data.recycleBinReminderEnabled);
    m_poseSize->setValue(data.poseSize);
    m_minigameEnabled->setChecked(data.minigameEnabled);
    // P8：预设对话 + 彩云天气
    m_dialogueEnabled->setChecked(data.dialogueEnabled);
    m_weatherKey->setText(data.weatherKey);
    m_weatherLocation->setText(data.weatherLocation);
    // EX 彩蛋：代码彩蛋
    m_codeEggEnabled->setChecked(data.codeEggEnabled);
    m_codeEggWorkspace->setText(data.codeEggWorkspace);

    // 小游戏插件：刷新各插件的「上次配置」摘要（摘要内容由插件自己决定）
    for (auto it = m_miniGameConfigLabels.constBegin(); it != m_miniGameConfigLabels.constEnd();
         ++it) {
        IMiniGamePlugin *plugin =
            (m_miniGames != nullptr) ? m_miniGames->find(it.key()) : nullptr;
        it.value()->setText(plugin != nullptr ? plugin->configSummary(m_db) : QString());
    }
    m_loading = false;

    refreshContent();
}

void SettingsDialog::refreshContent()
{
    if (m_content != nullptr) {
        m_content->refreshAll();
    }
}

void SettingsDialog::persist()
{
    if (m_loading || m_db == nullptr || !m_db->isOpen()) {
        return;
    }

    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    repo.load(data); // 保留窗口位置与 jsonExt 中的其它未知键
    data.petEnabled = m_petEnabled->isChecked();
    data.bubbleEnabled = m_bubbleEnabled->isChecked();
    data.particlesEnabled = m_particlesEnabled->isChecked();
    data.keywordAware = m_keywordAware->isChecked();
    data.nightQuiet = m_nightQuiet->isChecked();
    data.dragInertia = m_dragInertia->isChecked();
    data.recycleBinReminderEnabled = m_recycleBinReminder->isChecked();
    data.poseSize = m_poseSize->value();
    data.minigameEnabled = m_minigameEnabled->isChecked();
    // P8：预设对话 + 彩云天气（key / 城市都为空的组合即「不联网」）
    data.dialogueEnabled = m_dialogueEnabled->isChecked();
    data.weatherKey = m_weatherKey->text().trimmed();
    data.weatherLocation = m_weatherLocation->text().trimmed();
    // EX 彩蛋：代码彩蛋（工作区为空 = 不动作）
    data.codeEggEnabled = m_codeEggEnabled->isChecked();
    data.codeEggWorkspace = m_codeEggWorkspace->text().trimmed();

    if (!repo.save(data)) {
        qWarning() << "[SettingsDialog] 设置持久化失败";
    }
    emit settingsChanged(data);
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    emit visibleChanged(true);
}

void SettingsDialog::hideEvent(QHideEvent *event)
{
    QDialog::hideEvent(event);
    emit visibleChanged(false);
}

} // namespace whalepet
