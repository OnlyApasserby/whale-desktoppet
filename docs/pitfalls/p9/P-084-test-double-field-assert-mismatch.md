# P-084：测试断言把替身工厂的固定字段当成插件 id

- **原编号**：—（P9-C 阶段新增，序号接续 `P-083`）
- **模块**：tests / plugin（P9-C UI 宿主契约与贡献点协议）
- **报错分层**：测试失败（CTest Failed；非编译错误、非运行期崩溃）
- **严重度**：低

## 现象（可复现步骤 / 报错原文）

1. 新增 `tests/test_ui_plugin_host.cpp` 并注册 CTest 后，执行
   `ctest --test-dir build -C Debug -j1 --output-on-failure --timeout 120`；
2. `test_ui_plugin_host` 失败，其余 36 个目标通过。

报错原文（改用 `-o result_ui.txt,txt` 落盘后取得，逐字）：

```
FAIL!  : TestUiPluginHost::collectContributionsSkipsEmptyAndDuplicateIds() Compared values are not the same
   Actual   (list.at(0).pluginId)  : "test.fake"
   Expected (QStringLiteral("p.a")): "p.a"
F:\develop\desktoppet\tests\test_ui_plugin_host.cpp(124) : failure location
```

> 附带现象：CTest 的 `-o -,txt` 在该环境下未把失败详情带出，改用 `-o <file>,txt` 落盘才拿到原文——
> 与既有 `P-072`（CTest 失败时拿不到输出）同源，处置手法直接复用。

## 根因

用例 `collectContributionsSkipsEmptyAndDuplicateIds` 意图验证「重复贡献点 id 时**保留先注册者**」，
断言写成 `QCOMPARE(list.at(0).pluginId, QStringLiteral("p.a"))`——其中 `p.a` 是**插件** id。

但测试替身 `FakeUiPlugin` 的贡献点统一由工厂 `makeContribution()` 构造，
其 `PluginContribution::pluginId` 字段被硬编码为 `test.fake`。
`pluginId`（提供者插件）与「插件 id」不是同一来源，该断言与「先/后注册者」并无因果关系，必然失败。

被测实现 `PluginRegistry::collectContributions()` 行为正确（重复 id 确实保留了先注册者），
属**测试期望写错**，不是产品缺陷。

## 解决或规避

不放宽产品实现，改为断言**能区分先/后注册者的字段**：
同一 id 的两个贡献点 `order` 分别为 2（先注册）与 3（后注册），
断言 `QCOMPARE(list.at(0).order, 2)`（即「保留先注册者」）。
重建后单测通过，Debug / Release 全量回归恢复。

**可回灌规则**：断言应针对**被测逻辑的输入/输出区分字段**（本例为 `order`），
不得使用测试工厂的固定字段（本例为被硬编码的 `pluginId`）作为期望值。

## 影响与关联文档

- 影响：仅 P9-C 新增单测自身失败一次，无产品缺陷；已随本轮修复。
- 关联：`P-009`（断言写错、误判产品有 bug，同类）；`P-072`（CTest 失败无输出，本轮复用其规避手法）；
  `docs/TESTING.md`、`docs/ROADMAP-P9-Fin.md` §6.3 / §6.4。
