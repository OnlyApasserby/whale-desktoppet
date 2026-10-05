# `QStringLiteral` 只接受字面量

> **原编号**：`TRAP-P7-002`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：
  ```
  src\contextapi\builtin\ContextCapabilities.cpp(69,15): error C2146: 语法错误: 缺少“)”
      (在标识符“kContextCapabilitiesPluginId”的前面)
  src\contextapi\builtin\ContextCapabilities.cpp(69,15): error C2612: 基/成员初始值设定项列表中的非法后缀“)”
  ```
- **根因**：写了 `QStringLiteral(kContextCapabilitiesPluginId)`。`QStringLiteral` 是需要**字符串字面量**
  才能计算长度/类型的宏，不接受变量。
- **解决或规避**：改为 `QString::fromLatin1(kContextCapabilitiesPluginId)`；
  文件内其它「常量指针 → QString」的位置统一用 `fromLatin1`。
- **影响与关联文档**：`src/contextapi/builtin/ContextCapabilities.cpp`；
  `src/minigame/MiniGameCompatAdapter.cpp` 早已用 `QString::fromLatin1(kCapabilityPrefix)`（正确写法）。

---
