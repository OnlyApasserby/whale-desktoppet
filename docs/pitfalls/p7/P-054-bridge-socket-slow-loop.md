# 桥接 socket 的「单次未超时就继续」循环可被永久阻塞

> **原编号**：`TRAP-P7-016`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤）**：`bridge.path = "127.0.0.1:<port>"`，让对端**每 100ms 发 1 字节**
  且永不发 `\n`。`RpgMakerBridgeAdapter::read()` 永不返回——
  原循环是 `while (!buffer.contains('\n') && socket.waitForReadyRead(1500)) { buffer += readAll(); }`，
  每轮「等不到数据就当失败退出」，但外层只在**拿到换行**时才结束，
  于是 100ms < 1500ms 的慢速流让每轮都"成功超时"并继续，**总时长无上限**，
  GUI 线程被一个用户自备脚本永久占住。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2；`src/gamestate/RpgMakerBridgeAdapter.cpp`。
- **根因**：只约束了**单次**等待，没约束**总量**（字节数与总时长）；
  文件侧同样是无界 `file.readAll()`。
- **解决或规避**：新增三个常量（`RpgMakerBridgeAdapter.h`，可被单测直接核验）：
  `kBridgeMaxSnapshotBytes = 1 MiB`、`kBridgeSocketReadTimeoutMs = 1500`、
  **`kBridgeSocketTotalTimeoutMs = 4000`**。读循环改为「按剩余总时长切片
  `waitForReadyRead`」，字节累积超限即 `abort()`；文件侧改为**先看 `file.size()`
  再读，且多读 1 字节**识别「读取期间被替换/追加」。所有失败路径显式 `abort()`。
- **影响与关联文档**：`src/gamestate/RpgMakerBridgeAdapter.{h,cpp}`；
  `tests/test_gamestate_boundaries.cpp` 的 `bridgeSocketTimesOutOnSlowDripAndCloses`
  （断言耗时落在 `[总时长上限, 总时长上限+4s]` 区间）与
  `bridgeSocketRejectsUnterminatedOversizedStream`（断言**字节上限先于总时长生效**）。
