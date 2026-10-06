#pragma once

// P9-C：UI 贡献点协议（G2）——插件以**纯数据**描述希望出现在宿主界面上的入口，
// 由宿主（PetWindow）收集并按 kind 分发到右键菜单 / 托盘 / 设置页。
//
// 为什么用「纯数据 + std::function」而不是接口类：
//   * `whalepet_plugin` 只依赖 `Qt6::Core`，**不得**引入 `Qt6::Widgets`
//     （见 docs/ARCHITECTURE.md §A.2 G1、cmake/Libraries.cmake）；
//   * 因此 `QWidget` 仅以**前向声明**出现在回调签名中（指针类型不需要完整定义）；
//   * 视图的创建延迟到宿主真正需要时（`createView`），由 view 层的插件实现使用 Widgets 完成。
//
// 生命周期：贡献点中的 `std::function` 由插件持有；宿主只应在其存活期内调用
// （宿主通过 PluginRegistry 保证这一点）。

#include <QString>

#include <functional>

class QWidget;

namespace whalepet::plugin {

// 贡献点类型（宿主按此决定挂载位置）
enum class ContributionKind {
    ContextMenu, // 桌宠右键菜单项
    TrayMenu,    // 系统托盘菜单项
    SettingsTab, // 设置面板标签页
};

// 单个贡献点（值类型，可拷贝）。除 id / kind / label 外全部可选。
struct PluginContribution {
    QString pluginId; // 提供者插件 id（诊断用）
    QString id;       // 贡献点稳定 id（全局唯一；冲突时保留先注册者）
    ContributionKind kind = ContributionKind::ContextMenu;
    QString label; // 菜单文案 / 标签页标题
    int order = 100; // 排序（小者靠前；同序按插件注册顺序）

    // 菜单项（ContextMenu / TrayMenu）可选勾选态
    bool checkable = false;
    std::function<bool()> isChecked;      // checkable=true 时读取勾选态
    std::function<void(bool)> setChecked; // checkable=true 时写入勾选态

    // 菜单项点击回调（ContextMenu / TrayMenu）；为空时回退到 createView + 宿主展示
    std::function<void()> trigger;

    // 面板 / 标签页视图：延迟创建（首次展示时调用）；返回 nullptr 表示不可用。
    // parent 由宿主提供（view 层实现负责使用）。
    std::function<QWidget *(QWidget *parent)> createView;
};

} // namespace whalepet::plugin
