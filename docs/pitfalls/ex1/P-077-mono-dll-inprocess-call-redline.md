# 运行期调用 `mono.dll` 导出函数 == 在目标进程执行代码（红线决策）

> **原编号**：`TRAP-EX1-004`　**阶段**：EX1　**来源**：原按阶段聚合的 `traps-ex1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象/命题**：ROADMAP §2.6.1 原表述为「经 `mono.dll` 导出按类名/字段名解析」。
  但 Mono 的这些导出函数（`mono_class_from_name` / `mono_field_get_offset` / …）只能在
  **目标进程内**执行：跨进程实现必须「在目标进程创建远线程调用该函数」或注入一个模块，
  这属于**在目标进程执行代码**，等价于注入/hook。
- **根因**：Mono 无稳定的、可纯 `ReadProcessMemory` 复刻的公开结构（布局随版本变化），
  故「名字解析」的物理实现逃不开在目标进程内调用其 API。
- **解决或规避（决策）**：EX1.2 改为**离线名字解析**——
  `UnityDumpConverter` 解析 Il2CppDumper/Cpp2IL 的 `dump.cs`，按「类名+字段名」定位字段偏移并
  产出 profile；运行期 `UnityMonoAdapter` / `UnityIl2CppAdapter` 仅**消费 profile**（纯只读内存读取）。
  既满足「按名字定位、不硬编码偏移」的目标，又不触碰「不注入、不在目标进程执行代码」的红线。
- **影响与关联文档**：`src/gamestate/UnityDumpConverter.*`、`UnityMonoAdapter.*`、`UnityIl2CppAdapter.*`、
  `UnityAdapterBase.*`、`docs/UNITY-SOP.md`；关联 `docs/ROADMAP-ex1.md` §2.6.1 与 EX1.2 交付物 1 的折衷说明。
