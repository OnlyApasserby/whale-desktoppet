# 同步能力失败被误判为「异步已受理」（契约缺陷）

> **原编号**：`TRAP-P7-005`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：`test_plugin_registry::invokeRoutesAndReportsErrors` 失败，报错原文：
  ```
  FAIL!  : PluginRegistryTest::invokeRoutesAndReportsErrors()
     'registry.invoke(QStringLiteral("t.echo"), failParams, failCtx, failOut, failError)' returned FALSE.
  ```
  复现步骤：注册一个 `SimpleCapability` 子类，令其 `call()` 在收到 `{"fail": true}` 时填 error 并返回 `false`，
  然后调用 `CapabilityRegistry::invoke(...)`。
- **根因**：`ICapability::invoke` 的返回值语义是「**是否已同步完成**」：
  `true` = 已完成（成功见 `out`、失败见 `error`）；`false` = **异步已受理**。
  而 `SimpleCapability::invoke` 直接把 `call()` 的布尔值透传，于是「同步失败」被下游
  （`JsonRpcDispatcher` / 未来的通道）理解为「已受理，等回调」——**请求会永久挂起**，
  且不会有任何错误返回。这是接口适配层的语义错误，静态编译期无法发现。
- **解决或规避**：`SimpleCapability::invoke` 明确翻译为「同步」语义：
  ```cpp
  call(in, out, error);   // 结果（成功或失败）都写入 out / error
  return true;            // 对分发侧而言：本次调用已同步结束
  ```
  并把该契约写入 `Capability.h` 注释与 `docs/PLUGIN-ARCHITECTURE.md` §5，避免后续插件作者踩同样的坑。
- **影响与关联文档**：`src/plugin/Capability.cpp`、`src/plugin/Capability.h`；
  `docs/PLUGIN-ARCHITECTURE.md` §5；该缺陷由新增单测发现（**未放宽断言、未改用例**）。

---
