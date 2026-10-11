#pragma once

// P10 工作会话（工作能力插件）——口径见 docs/ARCHITECTURE.md 附录 D.4。
//
// 职责：持有「桌宠自己干活」的会话状态（时长 / 进度 / 产出），并供宿主与工作台面板驱动：
//   - 功能块清单来自 qrc **唯一索引**（默认 `:/python/blocks/index.txt` + `:<root>/<id>.txt`）；
//   - 会话期间由宿主把它交给「菜单互动」通道（PetController::handleMenuInteraction(Work)），
//     从而在桌宠上常驻 work-* 工作立绘（优先级仅低于深夜，见 core/MenuInteraction.h）；
//   - 到时（或「立即产出」）用 core::PythonBlocks::assembleScript 组装脚本，并以 QSaveFile
//     **原子写**到用户**显式指定**的输出目录（绝不写安装目录 / 数据目录）；
//   - 「戳一戳」在会话内**至多一次**把俏皮话插进脚本（P10 彩蛋迁移，见 core/CodeEasterEgg）。
//
// 安全边界：
//   - 输出目录为空 / 不存在 → 不产出（面板先提示），**绝不猜测目录**；
//   - 目标文件名已存在时顺延序号，**绝不覆盖**已有文件；
//   - 任一步失败只发 failed(reason)（会话回到 Idle），不影响桌宠其它功能。

#include "core/IRandom.h"
#include "core/PythonBlocks.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <cstdint>

namespace whalepet::viewmodel {

class WorkService : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,
        Running,
    };

    // 会话时长预设（分钟）：面板下拉与设置项共用同一真源
    static QVector<int> durationPresets();

    explicit WorkService(QObject *parent = nullptr);
    explicit WorkService(core::IRandom *rng, QObject *parent = nullptr);
    ~WorkService() override;

    // ---- 设置注入（宿主在 applyWorkSettings 里调用）----
    // work_enabled：会话期间「戳一戳」是否在脚本里插一句俏皮话（**默认关**）
    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }
    // work_output_dir：产出目录（空 = 未配置，start() 会拒绝）
    void setOutputDir(const QString &dir);
    QString outputDir() const { return m_outputDir; }
    // work_duration_min：会话时长（非预设值回落 25 分钟）
    void setDurationMinutes(int minutes);
    int durationMinutes() const { return m_durationMinutes; }

    // ---- 功能块（唯一索引）----
    const QVector<core::PythonBlock> &blocks() const { return m_blocks; }
    QString blocksRoot() const { return m_blocksRoot; }
    // 资源根路径；默认 ":/python/blocks"（单测可指向磁盘目录）
    void setBlocksRoot(const QString &root);
    void reloadBlocks();

    // ---- 会话状态 ----
    State state() const { return m_state; }
    bool running() const { return m_state == State::Running; }
    QStringList selectedBlocks() const { return m_selected; }
    int elapsedSec() const;
    int totalSec() const { return m_totalSec; }
    bool eggArmed() const { return m_eggArmed; }
    QString lastOutputFile() const { return m_lastOutputFile; }

    // 开始工作：blockIds 必须来自唯一索引；durationMsOverride > 0 时按毫秒计时（供测试）。
    // 失败时返回 false 并发 failed(reason)：未选功能块 / 输出目录不可用 / 会话已在运行。
    bool start(const QStringList &blockIds, int durationMsOverride = 0);
    // 取消会话（**不产出**）：无运行中会话时为空操作。
    void cancel();
    // 立即产出（等价于「到点」）：无运行中会话时返回 false。
    bool produceNow();

    // 「戳一下」：会话进行中 + 开关开启 + 本会话未触发过 → 标记并返回 true（**每次会话至多一次**）
    bool onPoke();

signals:
    void stateChanged();                                  // 会话状态 / 选中块 / 蛋标记变化
    void tick(int elapsedSec, int totalSec);              // 每秒一次（面板刷新进度）
    void finished(const QString &filePath, bool withEgg); // 产出成功
    void failed(const QString &reason);                   // 产出失败（会话已回到 Idle）

private:
    void setState(State state);
    void onSecondTick();
    bool produce();

    core::IRandom *m_rng = nullptr; // 非拥有；为空时回落内置 SystemRandom
    core::SystemRandom m_ownRng;
    QString m_blocksRoot = QStringLiteral(":/python/blocks");
    QVector<core::PythonBlock> m_blocks;
    QStringList m_selected;
    QString m_outputDir;
    bool m_enabled = false;
    int m_durationMinutes = 25;
    State m_state = State::Idle;
    std::int64_t m_elapsedMs = 0; // 会话已进行毫秒（由计时器累加，便于测试直接读）
    int m_totalSec = 0;
    bool m_eggArmed = false;
    QString m_lastOutputFile;
    QTimer m_timer;
};

} // namespace whalepet::viewmodel
