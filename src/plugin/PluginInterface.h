#pragma once

// 通用插件接口（泛化自 src/minigame/MiniGamePlugin.h 的既有语义）。
//
// 分层约定（与 docs/PLUGIN-ARCHITECTURE.md §4 一致）：
//   * 纯逻辑放 core/（零 Qt，可脱 UI 单测）；
//   * 能力以 CapabilityRegistry 注册，宿主与传输通道只按能力 id 驱动；
//   * 插件本身**无状态**（进程内单例），运行期依赖经 PluginContext 注入，
//     插件因此不依赖 PetWindow，便于独立演进与单测。
//
// 与 MiniGamePlugin.h 的对应关系：
//   MiniGameInfo    ↔ PluginInfo
//   MiniGameContext ↔ PluginContext
//   IMiniGamePlugin ↔ IPlugin
//   MiniGameView    ↔ 具体能力（前端/窗口由能力自行创建与管理）

#include "plugin/Capability.h"

#include <QString>

namespace whalepet {
class PetController;
}

namespace whalepet::model {
class Database;
}

namespace whalepet::plugin {

// 插件元数据（驱动诊断、能力清单展示与可选菜单入口）
struct PluginInfo {
    QString id;                            // 稳定标识，如 "minigame"
    QString displayName;                   // 显示名
    QString description;                   // 说明文案
    QString version = QStringLiteral("1.0"); // 语义化版本（协商用）
    QString menuLabel;                     // 可选：菜单入口文案（空 = 无菜单入口）
};

// 宿主注入给插件的运行期依赖（全部可空：缺失时插件必须优雅降级，不得崩溃）
struct PluginContext {
    whalepet::PetController *controller = nullptr;    // 立绘 / 台词播报
    whalepet::model::Database *db = nullptr;          // 配置与数据持久化
    CapabilityRegistry *capabilities = nullptr;       // 插件间互相发现能力
};

// 插件接口：元数据 + 能力注册 + 生命周期钩子
class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual PluginInfo info() const = 0;

    // 把本插件提供的能力注册进注册表（注册顺序即清单展示顺序的稳定依据）
    virtual void registerCapabilities(CapabilityRegistry &registry) = 0;

    // 生命周期：start 返回 false 表示初始化失败（宿主只记录并跳过，不阻断其它插件）
    virtual bool start(PluginContext &ctx)
    {
        Q_UNUSED(ctx);
        return true;
    }
    virtual void stop() {}

    virtual bool started() const { return m_started; }
    void setStarted(bool started) { m_started = started; }

private:
    bool m_started = false;
};

// 便捷基类：只关心元数据的插件可直接继承
class SimplePlugin : public IPlugin {
public:
    explicit SimplePlugin(PluginInfo info)
        : m_info(std::move(info))
    {
    }

    PluginInfo info() const override { return m_info; }

private:
    PluginInfo m_info;
};

} // namespace whalepet::plugin
