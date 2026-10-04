#pragma once

// EasterEggService（experiment/easter-egg1）：把「戳一戳」的低概率彩蛋落到**用户的代码工作区**。
//
// 行为：一次 poke() 先掷 5% 概率，命中后在工作区里挑一个源码文件，
//      在它已有的注释段落里追加一句鲸鱼娘的俏皮话（见 core/CodeEasterEgg）。
//
// 安全边界（对应需求「不影响代码功能」）：
//   - **工作区为空 / 不存在 → 直接返回**，绝不猜测目录（绝不碰安装目录、数据目录或当前目录）；
//   - 只处理 .py/.c/.cpp/.h 且**只追加注释行**，不修改任何既有代码；
//   - 幂等标记（core::kCodeEggMarker）+ QSaveFile 原子写；
//   - 文件数 / 大小 / 目录深度 / 尝试次数均设上限，跳过二进制与常见构建、依赖目录；
//   - 任一步失败都只跳过该文件（降级），不影响桌宠其它功能。
//
// 随机源可注入（core::IRandom），故「触发 / 不触发 / 选中哪个文件」都可脱 UI 单测。

#include "core/IRandom.h"

#include <QObject>
#include <QString>

#include <cstddef>

namespace whalepet::viewmodel {

class EasterEggService : public QObject {
    Q_OBJECT
public:
    explicit EasterEggService(QObject *parent = nullptr);
    // 注入随机源（不接管所有权）；为空时回落到内置 SystemRandom
    explicit EasterEggService(core::IRandom *rng, QObject *parent = nullptr);
    ~EasterEggService() override;

    // 「戳一戳」的触发概率：需求固定为 5%
    static constexpr double kTriggerProbability = 0.05;

    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }

    // 目标工作区目录。空串 = 关闭彩蛋注入；非目录 / 不存在同样视作空。
    void setWorkspace(const QString &dir);
    QString workspace() const { return m_workspace; }

    // 一次「戳一戳」：先掷概率，命中则**在一个**文件里藏一句；返回是否真的藏进去了。
    bool poke();

    // 诊断：最近一次成功藏话的文件绝对路径 / 藏进去的句子（空 = 还没有过）
    QString lastFile() const { return m_lastFile; }
    QString lastSaying() const;

signals:
    void eggPlanted(const QString &filePath, const QString &saying);

private:
    bool roll();               // 走随机源掷 5%
    QString pickAndInject();   // 扫描工作区并注入一句，返回被改写的路径（空 = 未改写）

    core::IRandom *m_rng = nullptr; // 不持有
    core::SystemRandom m_ownRng;
    bool m_enabled = false;
    QString m_workspace;
    QString m_lastFile;
    std::size_t m_lastSayingIndex = 0;
    bool m_hasLastSaying = false;
};

} // namespace whalepet::viewmodel
