# 档位扩容后 `core + warm ≥ kCacheCapacity` → 预载逐出 core

> **原编号**：`TRAP-P8-005`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：设计 / 回归（测试暴露） ｜ **影响**：core 档（首帧 + 贴边，延迟最敏感）可能被 LRU 逐出，与"零延迟"设计意图冲突。

### 现象

`test_pose_assets` 三处失败：

```
FAIL! : tierKeysAreAllRegisteredPoses  Actual (core.size()): 15  Expected (12): 12
FAIL! : tiersDoNotOverlapAndFitCapacity  'core.size() + warm.size() < library.capacity()' returned FALSE
FAIL! : libraryPreloadsCoreTierSynchronously  Actual (library.warmCount()): 38  Expected (22): 22
```

### 根因

P8 为 R1–R4 新增了 3 张 core（`night` / `daily-pajama` / `running`）与 16 张 warm
（工作池补全 6 + 对话池 5 + 天气 5）。15 + 38 = **53 > kCacheCapacity(36)**，
预载过程会按 LRU 把最早的 core 成员挤出去 —— 而该容量存在的唯一目的正是"预载完成后 core 与 warm 共存不逐出"。

### 解决

两条同时做（不做任何"放宽断言"的掩盖）：

1. **精简档位**：`sleep` 移出 core（P8 起深夜空闲立绘是 `daily-pajama`，`sleep` 已无代码路径输出）；
   `failure` / `celebrate` / `levelup` 与长尾关键词、对话池 5 张、天气 5 张一律**按需加载**
   （问答为 8–15 分钟一次的低频路径，首次解码延迟不可感知）→ core **14** + warm **25** = 39；
2. **容量 36 → 40**：39 < 40 恢复"预载不互逐"；40 × 256² × 4 B = **10.0 MiB**，
   仍满足 `POSE-ASSETS.md` 的 M3 目标（≤ 10 MiB），故未突破既定指标。

同步更新 `test_pose_assets` 的 4 处硬断言（core 14 / warm 25 / capacity 40 / warmCount 25）。

### 影响与关联文档

`src/view/PoseLibrary.{h,cpp}`、`tests/test_pose_assets.cpp`；
`POSE-ASSETS.md`（B1 / B2′ / M2 / M3 / 附录 C）、`ROADMAP-P8.md` A10。

---
