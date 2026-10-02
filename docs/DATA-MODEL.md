# 数据模型设计（DATA-MODEL）

## 1. 存储选型与位置

- 引擎：**SQLite**，经 `Qt6::Sql`（QSQLITE 驱动，Qt 内建，零新依赖）。
- 位置：**安装目录同级 `data/`**，文件 `data/whalepet.db`。
- 写失败降级（安装目录只读 / UAC 场景）：
  1. 首选 `<安装目录>/data/`；
  2. 失败 → 退回 `<用户目录>/WhalePet/`；
  3. 仍失败 → 内存模式并日志告警（`data dir fallback`），不崩溃。
- **不迁移**参考项目历史数据（whale localStorage / DesktopPet JSON 一律忽略）。

## 2. 通用约定

- **表主键约定**：多数表用 `id INTEGER PRIMARY KEY`；**`meta`**（`key TEXT PK`）、**`achievements`**
  （`ach_id`）、**`quests`**（`quest_id`）、**`signin`**（`day_index`）用各自的业务主键。
  时间统一用 **Unix 毫秒（INTEGER）**。
- 单例状态表用 `id = 1` 固定行。
- 建表脚本与迁移按 `meta.schema_version` 管理（当前 `1`）。
- **`meta` 承接的键**（表结构不变，均属元信息）：
  - `schema_version`：当前 `1`。
  - `last_signin_day`：最近一次每日签到的自然日 key（`YYYY-M-D`，**月/日不补零**，沿用 whale `dayKey`）；
    空串 = 从未签到。跨天连续签到判定只用这一个键 + `pet_state.streak_days`（见 `ROADMAP-P3-Fin.md` §4）。
  - **其余键**（成就/小游戏统计与记账）：`stat.*` 计数器、`stat.mg_day`、`app.last_seen_ms`、
    `game.reward_day`、`game.reward_plays_today`、`game.best_ms_*` 等——
    命名与语义见 `MINIGAME-INTERFACE.md` §2.5 与 `core/Achievements.h` 的 `achStatKey`。

## 3. 表设计

### 3.1 `meta`（元信息 / 迁移）
| 字段 | 类型 | 说明 |
|---|---|---|
| key | TEXT PK | 如 `schema_version` |
| value | TEXT | 版本号 / 配置快照 |

### 3.2 `pet_state`（单例）
| 字段 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PK | 恒为 1 |
| level | INTEGER | 等级 |
| exp | INTEGER | 当前经验 |
| coins | INTEGER | 金钱（如启用） |
| mood | INTEGER | 心情 0–100 |
| affinity | INTEGER | 好感度 |
| satiety | INTEGER | 饱食度 0–100 |
| bond_level | INTEGER | 羁绊等级（解锁 Lv3/5/7） |
| companion_ms | INTEGER | 累计陪伴时长 |
| streak_days | INTEGER | 连续签到天数 |
| last_active_ms | INTEGER | 最近活跃时间 |
| updated_ms | INTEGER | 更新时间 |

### 3.3 `settings`（单例）
| 字段 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PK | 恒为 1 |
| pos_x / pos_y | INTEGER | 窗口位置 |
| pose_size | INTEGER | 立绘尺寸(px) |
| bubble_enabled | INTEGER | 台词气泡开关 |
| particles_enabled | INTEGER | 特效开关 |
| keyword_aware | INTEGER | 关键词表情开关（默认关，见 `CHAT.md`） |
| minigame_enabled | INTEGER | **小游戏统一门控**（默认开；关闭后隐藏扫雷 / 找小猫 / 国际象棋全部入口） |
| json_ext | TEXT | 扩展项（JSON，向后兼容新设置）；承载 `pet_enabled` / `night_quiet` / `drag_inertia`、小游戏难度与配置（`minigame_preset` / `minigame_custom_width` / `minigame_custom_height` / `minigame_custom_mines` / `kitten_difficulty` / `chess_engine_path` / `chess_difficulty` / `chess_human_is_white`）与 P7 开关（`work_aware_enabled` / `context_api_*` / `acp_*`）——`SettingsRepo` 合并写回并保留未知键，完整清单见 `SETTINGS.md` §2/§6 |

### 3.4 `achievements`
| 字段 | 类型 | 说明 |
|---|---|---|
| ach_id | TEXT PK | 成就标识 |
| unlocked | INTEGER | 是否解锁 |
| unlocked_ms | INTEGER | 解锁时间 |

### 3.5 `quests`（每日任务）
| 字段 | 类型 | 说明 |
|---|---|---|
| quest_id | TEXT PK | 任务标识 |
| slot | INTEGER | 0–2 槽位 |
| progress | INTEGER | 进度 |
| target | INTEGER | 目标值 |
| done | INTEGER | 是否完成 |
| claimed | INTEGER | 是否已领奖 |
| day_key | TEXT | 归属日期（YYYY-MM-DD，用于每日刷新） |

### 3.6 `signin`（周签到）
| 字段 | 类型 | 说明 |
|---|---|---|
| day_index | INTEGER PK | 0–6（周内格） |
| week_key | TEXT | 归属周（如 2026-W40） |
| signed | INTEGER | 是否签到 |
| reward_claimed | INTEGER | 里程碑奖励(1/3/7) |

### 3.7 `bond_diary`（成长日记）
| 字段 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PK | 自增 |
| kind | TEXT | 事件类型（bond_up / achievement / …） |
| detail | TEXT | 描述 |
| ts_ms | INTEGER | 时间 |

> 保留上限 80 条；同一天同类事件只记一条（沿用 whale 规则）。

### 3.8 `chat_history`（可选）
| 字段 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PK | 自增 |
| line_key | TEXT | 台词场景 |
| text | TEXT | 台词内容 |
| ts_ms | INTEGER | 时间 |

> 仅用于「最近发言」去重与成长日记参考；可裁剪保留最近 N 条。

### 3.9 `hotwords`（自定义热词，P6 追加）
| 字段 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PK AUTOINCREMENT | 自增；**即录入顺序 = 匹配优先级顺序** |
| word | TEXT NOT NULL UNIQUE | 用户热词（写入前归一化：`trim` + `toLower`） |
| keyword_id | TEXT NOT NULL | 目标关键词 id（须命中 `kKeywordRules`） |
| created_ms | INTEGER | 录入时间 |

> **不升 `schema_version`**：与 §3.4–§3.6（P4 表）同样处理——全部 DDL 都是
> `CREATE TABLE IF NOT EXISTS`，幂等，老库启动时自动补建，无需迁移分支
> （`src/model/Schema.cpp` 的注释即指向本节）。
> CRUD 在 `src/model/HotwordRepo.{h,cpp}`；匹配规则在 `core::ChatRules::matchKeyword`（纯逻辑），
> Repo 不做任何匹配判定。详见 `CHAT.md` §4。

## 4. 访问层

- `Database`：连接、建表、迁移、事务、降级。
- `Repositories`：每个表的 CRUD（`PetStateRepo` / `SettingsRepo` / `AchievementRepo` / `QuestRepo` / `SigninRepo` / `DiaryRepo` / `HotwordRepo`）。
- **写策略**：状态变更即时落盘（量级小）；批量结算走单事务。

**P3 已落地的实现落位**（`model/` 静态库 `whalepet_model`，只依赖 `Qt6::Core` + `Qt6::Sql`，不链接 Widgets）：

| 文档概念 | 实现文件 | 备注 |
|---|---|---|
| 目录选择与降级 | `src/model/DataPaths.{h,cpp}` | `isDirectoryWritable()` 用「实际写入临时文件再删除」探测；`resolveWith()` 为可注入版本，供单测 |
| 连接 / 事务 / 降级 | `src/model/Database.{h,cpp}` | `mode()` 暴露实际生效的 `StorageMode`；失败时降级到 `:memory:` 并 `qWarning` |
| 建表 / 迁移 | `src/model/Schema.{h,cpp}` | `kVersion = 1`；全部 DDL 幂等；库版本高于程序时只告警不写入 |
| `meta` 读写 | `Database::meta()/setMeta()` | 键见 §2 |
| `pet_state` CRUD | `src/model/PetStateRepo.{h,cpp}` | 单例行 `id = 1`，`INSERT OR REPLACE` |
| `settings` CRUD | `src/model/SettingsRepo.{h,cpp}` | `pos_x/pos_y` 为 `NULL` 表示「未设置」 |

> `achievements` / `quests` / `signin` / `bond_diary` / `chat_history` / `hotwords` 六张表
> **在 v1 脚本中一并建出**（`hotwords` 为 P6 追加，同样不升版本号），
> CRUD 分属 P4 / P6；这样后续阶段无需再追加 `schema_version` 迁移。
>
> `pet_state` 之外没有「养成」状态：`level`/`exp` 的推导规则见 `GAMEPLAY.md` §1 与 `ROADMAP-P3-Fin.md` §1。

## 5. 一致性

- 每日刷新以 `day_key` 判定（跨天自动重置任务槽与签到格）。
- 启动时读取单例状态；退出/关键事件后强制 flush。
