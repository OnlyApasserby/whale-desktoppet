# 测试策略（TESTING）

> 不沿用参考项目的测试体系（GTest / node:test）；**自行编写测试用例**，基于 **Qt6::Test**。面向个人使用，聚焦核心逻辑正确性。

## 1. 原则

- **只测核心逻辑**（`core/` + `model/`），UI 交互以最小冒烟为主。
- 所有测试命令**必须带超时**（禁止裸跑）：`ctest ... --timeout 120`，并在 CMake 中固化 `TIMEOUT`。
- 无显示环境用 `QT_QPA_PLATFORM=offscreen`；仍不可用则 `SKIP_RETURN_CODE 77` 并写明原因。
- 禁止以删除断言 / 注释用例 / 放宽比较 / 吞异常的方式让结果「变绿」。

## 2. 测试对象与用例

| 模块 | 用例要点 |
|---|---|
| `PetStateMachine` | 各事件转移；`AFK`/`SUCCESS`/`CURIOUS` 窗口超时回落；**主动说话**节流（≥6s）与**用户交互不节流**；表现批次序号（同一缓存态重放不改号、新事件递增、回落归零）；深夜静默；不打断规则。随机源用**固定序列 RNG**保证可复现 |
| `GrowthService` | 经验/升级曲线；心情/饱食边界（0/100 夹取）；饱食随时间衰减；连续签到跨天判定 |
| `AchievementService` | 39 项判定条件；重复解锁幂等；解锁写日记 |
| `QuestService` | 每日 3 槽刷新（跨 `day_key`）；领取幂等；进度累加 |
| `SigninService` | 周签到格；1/3/7 里程碑奖励 |
| `ChatService` | 场景选词；最近 N 条去重；关键词→`meme-*` 映射；开关关闭时不触发 |
| `Database` | 建表/迁移；单例读写；事务回滚；安装目录不可写时的降级路径 |
| `LineTable` | 台词文件解析；缺失文件降级（空表 + 日志） |

> **已落地的测试目标**（截至 P6，均在 CTest 注册、带 `TIMEOUT`）：
>
> | 目标 | 文件 | 对应上面哪一行 |
> |---|---|---|
> | `test_smoke` | `tests/test_smoke.cpp` | 冒烟 + 表现层去重 + `PetWindow`/`PoseView` |
> | `test_state_machine` | `tests/test_state_machine.cpp` | `PetStateMachine` |
> | `test_line_table` | `tests/test_line_table.cpp` | `LineTable`（多文件加载 + 状态机场景覆盖） |
> | `test_database` | `tests/test_database.cpp` | `Database`（建表 / 迁移幂等 / 单例往返 / 事务回滚 / 目录三级降级） |
> | `test_growth` | `tests/test_growth.cpp` | `GrowthService` + `core/GrowthRules`（升级曲线 / 增量表 / 夹取 / 饱食衰减 / 跨天签到 / 升级信号 / 持久化） |
> | `test_content` | `tests/test_content.cpp` | `AchievementService`/`QuestService`/`SigninService` + `DiaryRepo`（39 项判定 / 3 槽抽签 / 周签到里程碑 / 日记上限） |
> | `test_chat` | `tests/test_chat.cpp` | `ChatService` + `core/ChatRules`（分时问候 / 深夜静默 / 心情分层 / 羁绊跨档 / 21 项关键词映射与开关 / 节流与序号 / 真实语料覆盖与立绘存在性） |
> | `test_hotword` | `tests/test_hotword.cpp` | 自定义热词优先匹配 / 归一化去重 / 热词表 CRUD / 显式录入不受开关门控 |
> | `test_settings` | `tests/test_settings.cpp` | 设置项「生效」：`night_quiet`（`PetStateMachine`）/ `pose_size` 与 `particles_enabled`、`drag_inertia`（`PoseView`）/ `bubble_enabled`（`SpeechBubble`） |
>
> `test_line_table` / `test_chat` 通过编译宏 `WHALEPET_LINES_DIR` 直读 `assets/lines/` 全部语料，
> 用于校验「代码引用的场景 key 在语料里真有候选」。
> `test_database` / `test_growth` 都用 `QTEST_GUILESS_MAIN`（只需 `QCoreApplication`），
> 不创建任何 Widget，故 offscreen 与无显示环境都能跑。

## 3. 测试组织

- 每个模块一个测试可执行目标（`qt_add_executable`），注册到 CTest。
- 逻辑测试（`core/`）**不链接 Widgets**，可 headless 运行。
- 冒烟测试（可选）：`offscreen` 下创建 `PetWindow`、切一次 pose、退出。
- 表现层去重（`test_smoke`，需要 Qt）：同一 `PoseResult` 重复 present → 特效只迸发一次、台词只播一次；
  特效 500ms 强制间隔内丢弃且**不补播**；新台词**打断**上一条流式输出并从第一个字重来。

## 4. CMake 约定

```cmake
enable_testing()
qt_add_executable(test_statemachine tests/test_statemachine.cpp)
target_link_libraries(test_statemachine PRIVATE Qt6::Test <core_lib>)
add_test(NAME test_statemachine COMMAND test_statemachine)
set_tests_properties(test_statemachine PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
```

## 5. 运行

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir build -C Debug --output-on-failure --timeout 120
```

## 6. 覆盖目标（个人使用的务实标准）

- `core/` 逻辑：关键分支全覆盖（状态转移、边界夹取、幂等）。
- `model/`：读写与迁移路径可用。
- 不追求行覆盖数字，追求**关键行为**有断言。

## 7. 降级观测

- 任何跳过（如 offscreen 不可用、台词语料缺失）必须在 CTest/日志中写明原因，不得静默。
