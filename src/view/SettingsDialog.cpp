#include "view/SettingsDialog.h"

#include "minigame/MiniGameRegistry.h"
#include "model/Database.h"
#include "model/SettingsRepo.h"
#include "view/ContentPanel.h"
#include "view/PoseView.h"

#include <QCheckBox>
#include <QDebug>
#include <QFormLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QTabWidget>
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

    m_poseSize = new QSpinBox(box);
    m_poseSize->setRange(PoseView::kMinDisplaySize, PoseView::kMaxDisplaySize);
    m_poseSize->setSingleStep(20);
    m_poseSize->setSuffix(QStringLiteral(" px"));
    connect(m_poseSize, &QSpinBox::valueChanged, this, [this](int) { persist(); });
    form->addRow(QStringLiteral("立绘尺寸"), m_poseSize);

    outer->addWidget(box);

    auto *hint = new QLabel(
        QStringLiteral("关键词感知默认关闭；开启后仅在你主动录入热词 / 复制文本时匹配梗词。\n"
                       "「显示桌宠」关闭后窗口隐藏，可通过托盘或左下角入口唤回。"),
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
    m_poseSize->setValue(data.poseSize);
    m_minigameEnabled->setChecked(data.minigameEnabled);

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
    data.poseSize = m_poseSize->value();
    data.minigameEnabled = m_minigameEnabled->isChecked();

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
