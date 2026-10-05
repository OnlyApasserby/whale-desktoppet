# ROADMAP · P3 — 数据层与养成核心

> ✅ **已完成阶段（2026-09-30 验收通过）**，已改签为 `ROADMAP-P3-Fin.md`。

## 阶段目标

落地 SQLite 数据层，接通养成核心数值，替换 P1 的临时持久化。

## 交付物

- `model/Database` + `Schema` + `Repositories`（按 `DATA-MODEL.md`）。
- `viewmodel/GrowthService`：心情/好感/饱食/等级/陪伴时长/连续签到。
- 状态面板（数值展示）。

## 任务清单

1. 实现 `Database`：连接、建表、`schema_version` 迁移、事务。
2. 实现数据目录选择与降级（安装目录 `data/` → 用户目录 → 内存 + 日志）。
3. 迁移 P1 的窗口位置持久化到 `settings` 表。
4. 实现 `PetStateRepo` / `SettingsRepo`。
5. 实现 `GrowthService`：
   - 经验累积与升级曲线；
   - 心情/好感/饱食变化与 0–100 夹取；
   - 饱食随时间衰减（定时结算）；
   - 陪伴时长累计、连续签到跨天判定。
6. 数值变化驱动状态机（升级 → 庆祝姿态）。
7. 单测：升级曲线、边界夹取、衰减、跨天签到、DB 读写与降级。

## 验收标准

- [ ] 重启后等级/心情/好感/饱食/位置均正确恢复。
- [ ] 投喂/夸夸/戳一下/摸头对数值有正确影响并落库。
- [ ] 安装目录不可写时自动降级且不崩溃，日志可观测。
- [ ] 数据层单测通过。

## 依赖

- P1（外壳）、P2（状态机驱动表现）。

---

## P3 设计补充（实施前锁定）

> 依据：`DATA-MODEL.md`（表结构）+ `GAMEPLAY.md`（数值）+ `ARCHITECTURE.md`（分层），
> 以及 whale 参考实现 `assets/whale-moe-core.js` 的**实测常量**（本次已核对源码，非推测）。

### 1. 升级曲线（对 `GAMEPLAY.md` §1 的落实，含一处澄清）

whale 实现中**没有独立的经验字段**：等级由累计好感推导，`LEVEL_STEP = 500`（`whale-moe-core.js:475-478, 574-575`）：

```js
level = Math.max(1, Math.floor(g.affinity / GROWTH.LEVEL_STEP) + 1);
```

本项目 `DATA-MODEL.md` 的 `pet_state` 同时定义了 `exp` 与 `affinity` 两列，故约定为：

- `exp` = **升级用累计经验**；`level = floor(exp / kLevelStep) + 1`，`kLevelStep = 500`（沿用 whale）。
- `affinity` = **好感度**（0–10000），用于羁绊与后续选词；互动时与 `exp` **同步累加**同一增量
  （即鲸鱼娘语义下「经验即好感」），因此 `level` 与 `bond_level` 在数值上一致，但两列职责不同、各自保留。
- `expNeeded(level)` 返回「升到下一级所需的**累计**经验」= `kLevelStep * level`，对 `level` **单调递增**，
  满足 `GAMEPLAY.md` 的表述；`expInLevel(exp)` 返回当前级内进度（供状态面板进度条）。
- 上一级所需经验改为可调（后续若要走「递增阶梯」，只改 `expNeeded()` 一处）。

### 2. 数值增减（沿用 whale 原值，`whale-moe-core.js:542-573`）

| 交互 / 事件 | mood | affinity(+exp) | satiety | 备注 |
|---|---|---|---|---|
| `pat`（摸头） | +4 | +2 | — | 分区摸头 |
| `poke`（戳一下） | −6 | — | — | |
| `feed`（投喂） | +3 | +5 | +30 | |
| `praise`（夸夸） | +5 | +8 | — | |
| `belly`（摸肚子） | +3 | +2 | — | |
| `tail`（戳尾巴） | +2 | +3 | — | |
| `triple`（三连击） | +10 | +10 | — | |
| `signin`（每日签到，每日首次） | +5 | — | — | 跨天判定见 §4 |
| `tick`（时间结算） | — | — | `−deltaMin × 0.15` | 每 60s 结算一次 |

- 夹取规则：`mood/satiety ∈ [0, 100]`，`affinity ∈ [0, 10000]`，`exp/level/bond_level` 单调不减。
- 常量集中放在 `src/core/GrowthRules.h`（**零 Qt 依赖**，可脱 UI 单测）。

### 3. 饱食衰减与陪伴时长

- 饱食衰减：`kSatietyDecayPerMin = 0.15`；**每 60s** 结算一次，`deltaMin` 取实际经过分钟数
  （参考实现 `references/dsh-whale-musume/assets/whale-moe-core.js:552`，`tick` 分支）。结算间隔常量 `kGrowthTickMs = 60000`。
- **落地形态**：`pet_state.satiety` 为 INTEGER（`DATA-MODEL.md` §3.2），直接按浮点衰减会在每次
  落盘时被取整而**永久丢失**（0.15 点/分钟 < 1 点）。故实现改用**整数等价形式**：
  `kMsPerSatietyPoint = 400000`（= 60000 / 0.15），即每 400s 稳定掉 1 点；
  不足 1 点的余量以毫秒累加在内存（`m_satietyAccumMs`），不落库——重启最多丢失 1 点衰减。
  投喂（`satiety` 增加）时余量清零，避免「刚喂饱立刻掉点」。
- 陪伴时长：`companion_ms` 由运行时累计（`last_active_ms` 与启动时刻差值累加），
  里程碑 1/7/30 天仅用于 P4 成就，本阶段只负责**累计与落库**。

### 4. 连续签到跨天判定（`whale-moe-core.js:553-562`）

- 以本地自然日 `day_key`（`YYYY-M-D`，月从 1 计）判定；当天首次签到才计。
- 若「上次签到日 == 昨天」→ `streak_days + 1`；否则重置为 `1`；每次签到 `mood +5`。
- `day_key` 的格式化在 `core/GrowthRules.h` 提供（不依赖 QDateTime，便于单测注入 `nowMs`）。

### 5. 数据目录与降级（落实 `DATA-MODEL.md` §1）

1. 首选 `<安装目录>/data/`（可写性用「实际写入临时文件再删除」探测，不依赖权限位）；
2. 失败 → `<用户目录>/WhalePet/`（`QStandardPaths::AppDataLocation`）；
3. 仍失败 → **内存模式** `QSQLITE` + `:memory:`，`qWarning()` 输出 `data dir fallback`，**不崩溃**。

- 降级结果由 `Database::mode()` 暴露，供日志与设置面板显示。
- 目录探测与连接串解析独立成 `model/DataPaths`，可脱数据库单测。

### 6. 模块与命名

- 新增静态库 `whalepet_model`：`Qt6::Core` + `Qt6::Sql`（**不链接 Widgets**），依赖 `whalepet_core`，可 headless 测试。
- 命名空间：`whalepet::model`；纯规则在 `whalepet::core`。
- 文件落位：
  - `src/core/GrowthRules.h`（纯规则/常量）
  - `src/model/DataPaths.h/.cpp`、`src/model/Database.h/.cpp`、`src/model/Schema.h/.cpp`
  - `src/model/PetStateData.h`、`src/model/SettingsData.h`
  - `src/model/PetStateRepo.h/.cpp`、`src/model/SettingsRepo.h/.cpp`
  - `src/viewmodel/GrowthService.h/.cpp`
  - `src/view/StatusPanel.h/.cpp`（状态面板，非模态 `QDialog`）

### 7. 迁移 P1 位置持久化（任务 3）

- `QSettings("WhalePet")` 的 `window/position`、`window/visible` **改为**经 `SettingsRepo` 读写 `settings` 表（`pos_x/pos_y`）。
- 首次启动做一次**一次性导入**：若 `settings.pos_x` 为空且 `QSettings` 存在旧值，则写入并记录日志；
  导入成功后不再读取 `QSettings`（避免双写）。
- 位置越界仍按夹回规则处理（`SETTINGS.md` §5）。*（原文写作 `PetWindow::restorePosition()`，
  该函数已按 `docs/pitfalls/` `TRAP-EXT0-002` 删除；现由 `PetWindow::defaultPosition()` /
  `clampToVisibleArea()` / `resetToDefaultPosition()` 承担。）*
- **`window/visible` 不再持久化**：P1 只写不读（启动恒 `show()`），是死配置；P3 起一并移除，
  不占用 `settings.json_ext`。

### 8. 数值 → 状态机（任务 6）

- `GrowthService` 发出 `levelUp(int level)` / `bondUp(int level)` 信号；
- `PetController` 收到后投递 `core::EventType::LevelUp`，由状态机给出庆祝姿态 + `Fx::Star`。

### 9. 本阶段**不做**（留给 P4/P6）

- 成就、每日任务、周签到板、成长日记的**判定与展示**（P4）。
- `SettingsDialog` 全部开关面板（P6）；P3 的「状态面板」只做**数值展示**，不含设置项编辑。

---

## 实现状态

本轮（P2 收尾同步推进）已完成全部 P3 交付物，Debug / Release 双配置构建 + CTest 全绿。

### 交付物对照

| # | 交付物 | 落位 | 状态 |
|---|---|---|---|
| 1 | `Database`（连接 / 建表 / 事务 / 降级） | `src/model/Database.{h,cpp}`、`src/model/DataPaths.{h,cpp}` | 完成 |
| 2 | `Repositories`（`PetStateRepo` / `SettingsRepo`） | `src/model/PetStateRepo.{h,cpp}`、`src/model/SettingsRepo.{h,cpp}` | 完成 |
| 3 | `GrowthService`（心情 / 好感 / 饱食 / 等级 / 陪伴 / 签到） | `src/viewmodel/GrowthService.{h,cpp}` + 纯规则 `src/core/GrowthRules.h` | 完成 |
| 4 | 状态面板（数值展示） | `src/view/StatusPanel.{h,cpp}`；由 `PetWindow` 右键菜单与托盘菜单「状态」打开 | 完成 |
| 5 | 单元测试（读写 / 迁移 / 回滚 / 降级；曲线 / 夹取 / 衰减 / 签到） | `tests/test_database.cpp`、`tests/test_growth.cpp` | 完成 |
| 6 | 迁移 P1 位置持久化 | `PetWindow::savePosition()/restorePosition()/importLegacyPositionIfNeeded()` | 完成 → **后已删除**（`docs/pitfalls/` `TRAP-EXT0-002`：启动恒居中，位置不再跨会话持久化） |
| 7 | 数值变化驱动状态机 | `PetController::setGrowthService()`：`levelUp`/`bondUp` → `EventType::LevelUp` → 庆祝姿态 | 完成 |

### 跨模块改动

- 新增静态库 `whalepet_model`（`Qt6::Core` + `Qt6::Sql`，**不链接 Widgets**，可 headless 测试）。
- `CMakeLists.txt` 新增**构建期** SQLite 插件检查（缺 `sqldrivers/qsqlite` 直接 `FATAL_ERROR`），
  与既有 WebP 插件检查同风格——避免「静默降级到内存库」这种「能跑但错」。
- `PetController` 新增可选注入点：不注入 `GrowthService` 时行为与 P2 **完全一致**，
  既有 `test_smoke` / `test_state_machine` 不受影响。

### 验收结果（2026-09-30，本机）

| 项 | 命令 | 结果 |
|---|---|---|
| Debug 构建 | `cmake --build build --config Debug --parallel` | 通过 |
| Release 构建 | `cmake --build build --config Release --parallel` | 通过 |
| Debug CTest | `ctest --test-dir build -C Debug` | **5/5 Passed** |
| Release CTest | `ctest --test-dir build -C Release` | **5/5 Passed** |
| 启动冒烟 | offscreen 平台启动 `WhalePet.exe` 观察 6s | 进程存活未退出；`build/Release/data/whalepet.db` 已生成（53248 字节，含 v1 全部表与 `schema_version`） |

> 运行 CTest 前须注入 Qt `bin` 并设 `QT_QPA_PLATFORM=offscreen`（见 `TRAP-P2-008`）。
> `build/Release/data/` 是应用真实的「安装目录同级数据目录」，属构建产物，不入仓库。
> **注**：本表为 P3 编码完成时的记录；收尾后 Release 产物目录已统一到 `deploy-release/`（见下节）。

### 人工复验结果（2026-09-30，真实桌面）

| # | 复验项 | 结果 | 说明 |
|---|---|---|---|
| 1 | 数值实时性 | ✅ 通过 | 摸头 / 戳一下 / 投喂 / 夸夸后面板数值立刻变化 |
| 2 | 跨重启一致 | ✅ 通过 | 退出再启动后位置与数值均恢复 |
| 3 | 面板可读性 | ✅ 通过 | 黑底白字下文字与进度条清晰、不被立绘遮挡 |
| 4 | 签到语义 | ✅ 通过 | 同日重复点击无变化；跨天首次点击 `+1` |
| 5 | 降级文案 | ✅ 通过 | `data/` 不可写时面板显示已降级，程序不崩 |

**合计：5/5 通过。**

### 部署与产物目录（收尾同步）

本阶段收尾时按用户要求把**部署目录与 Release 产物目录统一**：

- `CMakeLists.txt` 新增 `WHALEPET_DEPLOY_DIR`（默认 `${sourceDir}/deploy-release`）并设置
  `WhalePet` 的 `RUNTIME_OUTPUT_DIRECTORY_RELEASE` → **Release 版 `WhalePet.exe` 直接生成在
  `deploy-release/`**，与 `windeployqt` 拷贝的 Qt 运行库同目录，省掉「先 `Copy-Item` 再部署」。
- Debug 产物仍在 `build/Debug/`；测试可执行文件仍在 `build/<Config>/`（不污染部署目录）。
- `windeployqt` 重新部署后补齐了 `Qt6Sql.dll` 与 `sqldrivers/qsqlite.dll`
  （此前部署目录缺 SQL 驱动，会静默降级为内存库，见 `docs/pitfalls/` `TRAP-P3-003`）。
- 部署目录冒烟**不能再用 `QT_QPA_PLATFORM=offscreen`**（缺 `platforms/qoffscreen.dll`，
  会「进程存活但什么都没跑」），须用默认平台 + 产物断言，见 `docs/pitfalls/` `TRAP-P3-005`。

### 收尾验证（2026-09-30）

| 项 | 命令 | 结果 |
|---|---|---|
| Release 构建 | `& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Release --parallel` | 通过；产物落在 `deploy-release\WhalePet.exe` |
| 部署同步 | `windeployqt --release --no-translations --compiler-runtime --dir .\deploy-release .\deploy-release\WhalePet.exe` | 通过；新增 `Qt6Sql.dll`、`sqldrivers/qsqlite.dll` |
| 部署冒烟（默认平台，干净 PATH） | 启动 `deploy-release\WhalePet.exe` 观察 8s | 存活；线程 30、WS ≈ 100.9MB；生成 `deploy-release\data\whalepet.db`（53248 字节） |
| Release CTest | `ctest --test-dir build -C Release` | **5/5 Passed** |
| Debug CTest | `ctest --test-dir build -C Debug` | **5/5 Passed** |

## 完成标记

✅ **已完成** —— 2026-09-30 人工复验通过（5/5），本文件改签为 `ROADMAP-P3-Fin.md`。


