#pragma once

// P9-C：UI 宿主上下文（G1）—— 宿主向插件暴露的最小界面能力集合：
// 父窗口 / 原生句柄 / 生命周期回调 / 布局刷新 / 面板展示。
//
// 与 plugin 库的零 Widgets 约束一致：`QWidget` / `QObject` 仅前向声明，
// 全部以指针 / 回调表达（见 PluginContribution.h 顶部说明）。
//
// 边界：本接口**不承载任何业务语义**（不认识养成 / 对话 / 小游戏），
// 只提供「创建界面所需的环境」，因此不会把宿主业务分支引入插件总线。

#include <functional>

class QObject;
class QWidget;

namespace whalepet::plugin {

class IPluginUiHost {
public:
    virtual ~IPluginUiHost() = default;

    // 宿主主窗口对象（作为插件创建对象的 QObject 父；view 层可 static_cast 为 QWidget）
    virtual QObject *hostObject() const = 0;
    // 宿主主窗口（作为 QWidget 父；无界面宿主可返回 nullptr）
    virtual QWidget *hostWidget() const = 0;
    // 原生窗口句柄（HWND 等；无则 nullptr）
    virtual void *nativeWindowHandle() const = 0;

    // 插件注册「宿主关闭」回调（宿主在退出 / 析构前调用，用于插件清理）
    virtual void addShutdownCallback(std::function<void()> callback) = 0;
    // 插件在运行期动态增删贡献点后，请求宿主重建菜单 / 标签页
    virtual void requestRelayout() = 0;
    // 当前是否处于首次布局（插件可据此决定是否提前创建视图）
    virtual bool initialLayout() const = 0;

    // 请宿主展示一个由插件创建的面板（独立窗口；宿主统一管理置顶 / 激活语义）
    virtual void presentPanel(QWidget *panel) = 0;
};

} // namespace whalepet::plugin
