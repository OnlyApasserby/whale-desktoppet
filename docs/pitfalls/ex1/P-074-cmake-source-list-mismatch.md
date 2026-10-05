# CMake 生成失败——声明与列名不一致导致源文件缺失

> **原编号**：`TRAP-EX1-001`　**阶段**：EX1　**来源**：原按阶段聚合的 `traps-ex1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：`cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"`
  在 Generate 阶段报错（逐字）：
  ```
  CMake Error at D:/Qt-debug/lib/cmake/Qt6Core/Qt6CoreMacros.cmake:2804 (add_library):
    Cannot find source file:
      F:/develop/desktoppet/build/src/gamestate/Win32GameMemoryReader.h
  Call Stack (most recent call first):
    ...
    CMakeLists.txt:168 (qt_add_library)
  ```
  可复现：`CMakeLists.txt` 第 172 行把 `src/gamestate/Win32GameMemoryReader.h` 列为源文件，
  但磁盘上不存在该头文件（只有 `.cpp`）；`Win32GameMemoryReader` 类声明被写进了
  `IGameMemoryReader.h`，而 `Win32GameMemoryReader.cpp` 却 `#include "gamestate/Win32GameMemoryReader.h"`。
- **根因**：接口抽象与具体实现被合并在同一个头里，导致「CMake 源列表 / `.cpp` 的 include /
  头文件实际位置」三者不一致；CMake 在 Generate 阶段即校验源文件存在性并硬失败。
- **解决或规避**：把 `Win32GameMemoryReader` 类从 `IGameMemoryReader.h` 拆出，新建独立头
  `src/gamestate/Win32GameMemoryReader.h`（`#include "gamestate/IGameMemoryReader.h"`）；
  在 `IGameMemoryReader.h` 末尾 `#include "gamestate/Win32GameMemoryReader.h"`，
  保持既有「仅含 `IGameMemoryReader.h`」的消费者（`test_game_memory*`、`GenericChainAdapter.cpp`）
  仍可见具体类，无需改动。重新 configure 通过。
- **影响与关联文档**：`src/gamestate/IGameMemoryReader.h`、`src/gamestate/Win32GameMemoryReader.h`、
  `CMakeLists.txt`（`whalepet_gamestate`）；关联 `docs/ROADMAP-ex1.md` EX1.1、§六 6.2。
