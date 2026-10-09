# P-092 · 移除静态库目标后，复用旧构建目录出现 MSB8064 增量依赖告警

- **模块**：`cmake/Libraries.cmake`、`cmake/Tests.cmake`
- **报错分层**：build（增量生成）
- **严重度**：minor
- **原编号**：—（EX3 新增）

## 现象（可复现步骤 / 报错原文）

EX3 移除 `whalepet_gamestate` 目标与 5 个测试目标、并把对应源文件移出 `src/` 后，按
「复用已有构建目录、不得清理现场」的约束（`workspace-manage` §2.2）**不删除** `build/`，
直接重新 configure + build。构建成功，但输出多条告警：

```
warning MSB8064: 项“…\build\CMakeFiles\<hash>\timestamp_(CONFIG).rule”的自定义生成成功，
但指定的依赖项“f:\develop\desktoppet\src\gamestate\gameprofile.h”不存在。
这可能会导致增量生成无法正常工作。 [F:\develop\desktoppet\build\test_game_companion_autogen.vcxproj]
```

同类告警还指向 `src/gamestate/igamestateadapter.h` 等已移除头文件。

## 根因

VS 生成器的 AUTOMOC 自定义生成规则把「上次 configure 时的头文件依赖列表」缓存在
`build/CMakeFiles/**/timestamp_(CONFIG).rule` 等规则文件里；`cmake` 重新生成 `.vcxproj`
但**不清理**这些残留规则文件，于是旧依赖路径仍指向已移出的文件。属**增量状态残留**，
不是代码或 CMake 语义错误。

## 解决或规避

- 已验证：重新 configure + 全量 build 后目标全部构建成功；Debug / Release CTest 各 **32/32** 通过，
  该告警不影响产物正确性。
- 规避建议：在**不删除构建目录**的前提下，如需消除告警，可对涉及的 `*_autogen.vcxproj` 触发一次重建，
  或删除 `build/CMakeFiles/<hash>/timestamp_(CONFIG).rule` 这类**纯生成物**（不属于「清理现场」范畴）。
  本轮按「现场保留」原则**未清理**，仅登记备查。

## 影响与关联文档

- 关联：`cmake/Libraries.cmake`（移除 `whalepet_gamestate`）、`cmake/Tests.cmake`（移除测试目标）、
  `docs/ARCHITECTURE.md` 附录 B（EX3 范围）。
- 约束依据：`workspace-manage` §2.2「复用现有构建目录、不得清理现场」与 `debug` §5.4「现场保留」。
