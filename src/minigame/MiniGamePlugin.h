#pragma once

// 小游戏插件接口层（**唯一**接入点）。
//
// 目标：让「新增一个小游戏」= 新增一个插件目录 + 在 MiniGameRegistry 的内置列表
// 注册一行，宿主（PetWindow）与结算服务（MiniGameService）**无需任何改动**。
//
// 分层约定：
//   * 纯玩法逻辑放 core/（零 Qt，可脱 UI 单测），产出 core::MiniGameResult；
//   * 界面实现 MiniGameView（QDialog 子类），通过 gameFinished 上报一局结果；
//   * 插件实现 IMiniGamePlugin（无状态工厂 + 元数据），负责组装二者。
//
// 宿主只按接口驱动：菜单/设置页文案取自 MiniGameInfo，窗口经 createView 创建，
// 结算统一走 MiniGameView::gameFinished → PetWindow 结算链路。

#include "core/MiniGameTypes.h"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

namespace whalepet {

namespace model {
class Database;
} // namespace model

class PetController;

// 宿主注入给插件的运行时依赖（插件不直接依赖 PetWindow，便于独立演进与单测）
struct MiniGameContext {
    PetController *controller = nullptr; // 立绘 / 台词播报（可空：无表现也能玩）
    model::Database *db = nullptr;       // 配置与纪录持久化（可空：等价内存态）
};

// 插件元数据（驱动菜单与设置页，不含任何玩法细节）
struct MiniGameInfo {
    QString id;          // 稳定标识（落库 / 查找用），如 "minesweeper"
    QString displayName; // 显示名，如 "扫雷"（设置页「开始××」按钮用它拼接）
    QString menuLabel;   // 「小游戏…」下拉列表内的菜单项文案，如 "扫雷"
    QString description; // 设置页说明文案
};

// 小游戏视图基类：宿主经由本基类装载 / 重开 / 回填结算文案。
// 所有小游戏窗口都必须继承本类（QDialog 子类，非模态由基类统一设置）。
class MiniGameView : public QDialog {
    Q_OBJECT
public:
    explicit MiniGameView(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setModal(false);
    }

    // 按持久化配置重开一局（宿主每次打开窗口前调用）
    virtual void reload() = 0;
    // 回填本局结算文案（奖励 / 纪录），由宿主在结算后调用
    virtual void setRewardText(const QString &text) = 0;

signals:
    // 一局结算（宿主据此发放养成奖励 + 上报成就 + 回填文案）。
    // 注意：不能命名为 finished——QDialog 已有 finished(int) 信号。
    void gameFinished(const whalepet::core::MiniGameResult &result);
};

// 小游戏插件：元数据 + 视图工厂。实现应无状态（进程内单例），
// 每个游戏窗口由宿主按需创建并接管所有权。
class IMiniGamePlugin {
public:
    virtual ~IMiniGamePlugin() = default;

    virtual MiniGameInfo info() const = 0;

    // 创建游戏窗口（parent 为宿主窗口，可为空；返回对象的所有权交给宿主）
    virtual MiniGameView *createView(const MiniGameContext &ctx, QWidget *parent) = 0;

    // 设置页展示的「上次配置」摘要（可选；默认空 = 不展示）
    virtual QString configSummary(model::Database *db) const
    {
        Q_UNUSED(db);
        return QString();
    }

    // 旧版本个人最快纪录的迁移表（可选）：每项为 (legacy 后缀, difficultyId)。
    // 升级后首次加载时，若新键为空而 meta `game.best_ms_<legacy 后缀>` 有值则继承，
    // 保证重构/改名不丢历史纪录。
    virtual QList<QPair<QString, QString>> legacyBestRecords() const { return {}; }
};

} // namespace whalepet

// 信号参数需可被 Qt 元系统识别（同线程直连亦依赖 moc 生成签名）
Q_DECLARE_METATYPE(whalepet::core::MiniGameResult)
