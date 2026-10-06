# P-081 能力模板把「服务指针」传给期望「插件指针」的快照回调 → C2440/C2664

- **阶段**：P9 渐进式插件化（主阶段/开发阶段）
- **模块**：src/viewmodel/builtin
- **报错分层**：build
- **严重度**：minor
- **日期 / 版本**：2026-10-05，工作区未提交（P9-A 实现轮次）
- **环境**：CMake 4.4.2 / VS 18 2026 / Qt 6.8.4 / PowerShell 5.1 / 生成器 Visual Studio 18 2026 / Debug
- **关联**：`docs/ROADMAP-P9-Fin.md` §P9-A；`src/viewmodel/builtin/ServiceStatusCapability.h`

## 一、问题描述

新增的能力模板 `ServiceStatusCapability<PluginT>` 把快照回调定义为
`std::function<QJsonObject(PluginT *)>`（接收**插件指针**），但 `call()` 内写成
`m_snapshot(m_plugin->service())`（传入**服务指针**）。编译报类型不匹配。

- 复现命令：`& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Debug --parallel`
- 复现率：必现
- 影响面：`whalepet_view` 编译失败，阻断 P9-A 全部交付（Debug / Release 均无法产出）
- 证据：
```
F:\develop\desktoppet\src\viewmodel\builtin\ServiceStatusCapability.h(58,15):
  尝试匹配参数列表“(whalepet::viewmodel::DialogueService *)”时
  ...
  在编译 类 模板 成员函数“ServiceStatusCapability<DialogueServicePlugin>::call(...)”时
```

## 二、原因分析

模板快照的类型契约是「插件指针」，而实参是 `plugin->service()` 的返回值（服务指针）；
二者类型无关，`std::function` 无法完成转换，MSVC 报 C2440/C2664，并在模板实例化链中
打印 `ServiceStatusCapability<DialogueServicePlugin>` 的上下文。

- 定位过程：只看**首条** error，按「能力模板 → `std::function` 签名」分层即可直接落到
  `ServiceStatusCapability.h:58`；被排除的假设是 `QJsonObject` 赋值转换问题（错误位置与
  `out =` 赋值无关，且 C2440 出现在参数匹配阶段）。
- 根因：模板参数语义（`PluginT*`）与实参（`Service*`）不一致——同一份「快照」在定义处与
  调用处对参数含义的理解相反。

## 三、解决方案

`call()` 改为传**插件指针**：`out = m_snapshot(m_plugin);`，由快照回调内部自行
`plugin->service()` 取服务。附带好处：能力不缓存服务实例，服务 `start()/stop()` 后
能力自动反映最新状态（未启动时返回「不可用」）。

- 验证：原命令 `cmake --build build --config Debug --parallel`，退出码 0
- 回归：Debug 全量 CTest **36/36**（顺序）；Release 构建退出码 0
- 降级：无

## 四、预防措施

- 模板化回调先明确「参数是插件指针还是服务指针」，并在 `call()` 与 `Snapshot` 定义处保持同源；
  源码已在 `ServiceStatusCapability::call()` 处补注释：「快照取**插件指针**（回调内再经
  `plugin->service()` 读取，避免能力缓存服务实例）」。
- 回灌规则：无（一次性实现失误，已由源码注释固化）。
