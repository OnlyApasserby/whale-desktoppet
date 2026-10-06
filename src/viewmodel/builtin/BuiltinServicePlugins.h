#pragma once

// P9-A：宿主服务经 builtin 层注册化（docs/ROADMAP-P9.md §P9-A、docs/ARCHITECTURE.md §A.6）。
//
// 背景：GrowthService / StomachService / DialogueService / EasterEggService / RecycleBinService
// 本体均**零界面依赖**，此前由 `PetWindow` 逐个 `new` 并手工接线（22 个 `setup*` 中的 5 个）。
// 本文件把它们的「创建 + 逻辑型接线 + 能力注册」收敛为 builtin 插件：
//   * 插件归属 `whalepet_view`（与 MiniGameCompatAdapter 同构），因此能力总线
//     （`whalepet_plugin`）**不反向依赖** view / model；
//   * 服务的 QObject parent 由宿主注入（`host`），生命周期与既有一致；
//   * 插件只做「逻辑型」接线；**UI 反应**（状态面板刷新 / 对话面板 / 托盘气泡）仍由宿主连接；
//   * 插件实现需自行 include 宿主类型头——`PluginContext` 中的 `PetController` / `Database`
//     为**前向声明**（见 src/plugin/PluginInterface.h），解引用成员前必须补 include。
//
// 边界（2026-10-05 修订）：P9-A / P9-B 验收通过后 P9-C 已启动，后续将引入通用 UI 宿主契约
//   （G1 `IPluginUiHost`）与贡献点协议（G2）；本文件当前仍只提供**窄回调**（`BuiltinServiceHooks`）。

#include "plugin/PluginRegistry.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace whalepet {

namespace viewmodel {
class DialogueService;
class EasterEggService;
class GrowthService;
class RecycleBinService;
class StomachService;
} // namespace viewmodel

// 插件 start() 后回填的服务句柄；宿主据此做 UI 反应接线与只读访问。
// 全部为**非拥有**指针：服务对象的 QObject parent 是注册时注入的 host（宿主窗口）。
struct BuiltinServiceHandles {
    viewmodel::GrowthService *growth = nullptr;
    viewmodel::StomachService *stomach = nullptr;
    viewmodel::DialogueService *dialogue = nullptr;
    viewmodel::EasterEggService *easterEgg = nullptr;
    viewmodel::RecycleBinService *recycleBin = nullptr;
};

// 宿主注入的窄回调：只承载「读取宿主 UI 状态」的门槛判断，不含通用 UI 契约。
struct BuiltinServiceHooks {
    // 预设对话的主动提问门槛（桌宠可见 / 气泡空闲 / 无面板占用 / 非工作 busy / 非深夜）。
    // 为空时 DialogueService 视为「始终允许」（与既有 setCanAsk(nullptr) 的语义一致）。
    std::function<bool()> dialogueCanAsk;
};

// 把 5 个宿主服务插件注册进能力总线（builtin 层），返回注册成功数；参数非法返回 -1。
//   host    ：服务 QObject 的 parent（宿主窗口；存活期须覆盖插件）；
//   hooks   ：宿主回调（按引用取地址保存，生命周期须覆盖插件存活期）；
//   handles ：服务句柄回填结构（同上）。
int registerBuiltinServicePlugins(plugin::PluginRegistry &registry, QObject *host,
                                  const BuiltinServiceHooks &hooks,
                                  BuiltinServiceHandles *handles);

// 服务只读状态能力 id（唯一真源；供可用性门控与测试核对，避免 id 漂移）。
QStringList builtinServiceCapabilityIds();

} // namespace whalepet
