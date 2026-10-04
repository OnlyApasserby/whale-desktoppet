# poses 资源利用率提升方案（POSE-ASSETS）

> 目标：把 `assets/poses/` 下 93 张立绘从「**打包即弃的静态仓库**」升级为「**有索引、有分类、有度量、有门禁、可激活**」的受管资源池。
> 关联：`PRESENTATION.md`（立绘资产）、`STATE-MACHINE.md`（姿态语义）、`traps-extend0.md`（`TRAP-EXT0-003` 三方同步 / QTest 日志丢失）、`traps-P5.md`（禁止拼接名字）、`TESTING.md`。

## 实施状态（2026-10-04）

| 阶段 | 状态 | 说明 |
|---|---|---|
| **A · 索引与度量** | 🟡 部分落地 | A1–A4、A7 已由 `tests/test_pose_assets.cpp` 以**硬断言**替代（18 个用例覆盖三方一致性、尺寸/格式门禁、分档白名单、缓存行为）；A5（`kPoseCount` 改 `std::size` 派生）、A2（`poses.json` + 生成器入库）**未做**，仍待索引落地 |
| **B · 预载分档与内存回收** | ✅ **已落地** | 见下方「阶段 B 落地详情」 |
| C · 命名与分类规范化 | ⬜ 未开始 | — |
| D · 激活闲置资源 | ⬜ 未开始 | — |

### 阶段 B 落地详情（路径 A）

| 步骤 | 状态 | 实现 |
|---|---|---|
| B1 预载分档 | ✅ | `PoseLibrary::coreKeys()` **14 张**（P8：+`night` / `daily-pajama` / `running`，−`sleep`）+ `warmKeys()` **25 张**（P8：工作立绘池补全 6 张，`failure` / `celebrate` / `levelup` 改按需）（代码内显式声明；索引落地后改由 `poses.json` 的 `preload` 字段驱动） |
| B2 按档预载 | ✅ | `startPreload()`：core 同步加载 → warm 进 `QTimer(120ms)` 队列 → 其余**不预载**。预载量由 88 张降到 39 张（core 14 + warm 25），总耗时 ≈3s |
| B2' LRU 容量上限 | ✅ | `kCacheCapacity = 40`（P8：36 → 40；= 10.0 MiB），按最久未使用逐出（`take()` / `put()` 都会更新时间戳）。**容量必须严格大于 core + warm 之和**，否则预载会把 core 挤出去（见 `traps-P8.md` TRAP-P8-005） |
| B2'' 负缓存 | ✅ | `QHash<QString, PoseLoadError> m_failed`：失败只解码一次、只告警一次；回填即清除 |
| B2''' 按需加载回填 | ✅ | 新增 `PoseLibrary::ensureLoaded()`；`PoseView::loadSourcePixmap()` 全部委托给它（加载/校验/缓存单点化） |
| B3 严格尺寸与格式限制 | ✅ | 新增 `src/view/PoseImageLoader.{h,cpp}`：格式白名单 + **尺寸必须 256×256** + Qt 原生解码；详见下节 |
| B3' M3 实测 | ✅ | `residentBytes()` 埋点 + 单测断言，**10.0 MiB ≤ 10 MiB 目标达成**（P8：容量与档位同步扩容，仍未突破既定口径） |
| B4 `poses.json` 索引 | ⬜ | 未做（见「后续」）；当前档位白名单在 `PoseLibrary.cpp` |
| B5 A4 缩放结果缓存 | ❌ **已评估、判定不做** | 见「已评估但未实施项」 |

### 严格图片限制（用户明确要求，已落地）

三条硬约束集中在 `PoseImageLoader::loadChecked()`，任一不满足即拒绝（**不缩放、不裁剪、不静默降级**）：

| 约束 | 实现 | 拒绝分类 |
|---|---|---|
| **尺寸必须 256×256** | `QImageReader::size()` **在解码之前**读取文件头并严格比对 `kPosePixels`；解码后再比对一次（防御个别格式 `size()` 撒谎） | `SizeRejected` |
| **仅接受 Qt 原生支持的格式** | 后缀必须在 `allowedSuffixes()`（当前仅 `webp`）内 **且** 该后缀落在 `QImageReader::supportedImageFormats()` 内。**不引入任何额外格式解析**：无 libwebp / stb_image / 自写解码器 | `FormatRejected` / `Unreadable` / `DecodeFailed` |
| 解码失败可区分 | 五类原因（`UnknownKey` / `FormatRejected` / `Unreadable` / `DecodeFailed` / `SizeRejected`）带中文 detail 回报，日志与单测共用同一套分类 | — |

**附带收益**：尺寸闸门前置到解码之前，等于给「超大图」加了一道 **OOM 崩溃闸门**——旧版一张误放的 4000×4000 会被静默解码并常驻 64 MB，现在在分配像素缓冲之前就拒绝。这是路径 B（外部资源）实施前的**准入前提**。

### 连带修复（实施中发现）

| 项 | 说明 |
|---|---|
| `whalepetInitAssetsResource()` 收敛为单一定义 | 旧实现把同一段 `Q_INIT_RESOURCE(assets)` 在 `PoseView.cpp` 与 `main.cpp` 各写了一份，而 `PoseLibrary::loadOne()` **自身不做初始化**，只是恰好依赖 `main.cpp` 在构造 `PetWindow` 前抢先初始化过——一旦有入口先调 `startPreload()` 而没碰 `PoseView`，93 张会全部静默加载失败。现提取为 `src/view/AssetsResource.{h,cpp}`，三处共用（`loadOne()` 自行调用，库可独立驱动与单测） |
| `PoseLibrary` 诊断接口接线 | `loadedCount` / `failedCount` / `capacity` / `residentBytes` / `decodeAttempts` / `decodedTotal` 此前只有声明、零调用；现由 `test_pose_assets` 实际断言，并为将来设置面板「资源」页预留 |

### 已评估但未实施项

| 项 | 判定 | 理由 |
|---|---|---|
| **A4 缩放结果缓存** | ❌ 不做 | `m_render` / `m_staged` / `m_edgeRender` 已是缓存；日常换图时目标图首次必然要缩放，`setDisplaySize` 的 3 处重采样也已单独处理。额外缓存收益低而逐出逻辑风险非零 |
| **异步加载（`QtConcurrent`）** | ❌ 不做 | `find_package(Qt6 ...)` 未含 `Concurrent`；且 `QPixmap` 必须在 GUI 线程用，异步只能解 `QImage` 再 `fromImage`，会引入深拷贝，**净收益可能为负**。改以「单张解码远小于 16 ms 帧预算」为依据保留同步路径 |
| **DPR 修正** | ⬜ 独立项 | `PoseView::resolvePixmap()` 的 DPR 代码对 qrc 源图恒为 no-op（`devicePixelRatio()` 恒 1.0，且 `drawPixmap` 该重载不应用 DPR），高分屏下立绘实际模糊。与路径 A 正交，需单独处理 |

### 后续

1. **A2 索引**（`assets/poses/poses.json` + `tools/gen_pose_names.py`）：落地后 `PoseLibrary` 的两个白名单改由 `preload` 字段驱动，并顺带消除「漏改 qrc」这类静默缺图（`TRAP-EXT0-003`）。
2. **A5**：`kPoseCount` 改为 `std::size(kPoses)` 派生量，消除中间插入丢尾项。
3. **路径 B（外部资源加载）**：见 `feature/external-pose-assets` 分支。准入前提已由 `PoseImageLoader` 的尺寸/格式闸门提供。

---


## 一、背景与目标

### 1.1 现状一句话

`assets/poses/` 共 **93** 张 webp（合计 **2 057 152 B ≈ 1.96 MiB**，均值 23.2 KB，统一 256×256 VP8X），
其中 **55 张（59.1%）有真实代码引用路径**，**38 张（40.9%）零引用**（清单见附录 A）。
这 38 张**仍被 `PoseLibrary::startPreload()` 无条件全量预载并常驻内存**（估算 ≈ 9.5 MiB 无效 `QPixmap`），
且**全部 93 张都打进了可执行文件**（`assets/assets.qrc` 逐条登记）。

### 1.2 目标（可验收）

| # | 目标 | 度量 |
|---|---|---|
| G1 | 资源**有唯一真源索引**，三方（磁盘 / 清单 / qrc）同步由「人肉铁律」变为「机器门禁」 | 索引一致率 = 100%，可 CI 拦截 |
| G2 | 消除**零引用存量**：每张资源都有明确归类（`active`/`lazy`/`reserved`/`retired`） | 零引用存量 = 0 |
| G3 | **预载/内存与利用率挂钩**，不为永不显示的资源付成本 | 预载冗余率 = 0%，常驻立绘内存 ≤ 10 MiB |
| G4 | 命名与分类**成文、可校验**，新增资源有模板可依 | 命名合规率 = 100% |
| G5 | 闲置资源**转化为可用能力**或**显式退役** | 引用覆盖率 ≥ 95% |
| G6 | 建立**定期审查与更新机制** | CTest 门禁 + 季度审计报告 |

### 1.3 硬约束

1. **不破坏现有行为**：阶段 A/B 必须「零视觉回归、零接口变更」（`test_state_machine` / `test_smoke` 全绿）。
2. **不新增第三方依赖**：沿用 `docs/README.md` §5.1 口径（零第三方依赖，允许 Qt 官方模块）。
3. **不擅自删除美术资源**：任何 `retired` / 移出打包的处置**须经项目 owner 确认**（见 §六）。
4. **崩溃交回用户**：出现崩溃（`0xC0000409` / `0xC0000005`）按 `docs/README.md` §六 立即停止并交回用户，AI 不得自行调试。

---

## 二、定义、分类标准与使用现状

### 2.1 定义：三层模型

一个「姿势」由**三个同心层**组成，任一缺失即「静默缺图」或「静默退化」：

| 层 | 载体 | 权威位置 | 作用 |
|---|---|---|---|
| **L1 物理资源** | `assets/poses/dsh-whale-*.webp` | 磁盘 | 被解码的像素数据 |
| **L2 键值映射** | `key → file` | `src/core/PoseNames.h` 的 `kPoses[]`（L13–107）+ `kPoseCount`（L109） | 语义名 ↔ 文件基名的**唯一真源** |
| **L3 语义引用** | `pose key` 字符串 | `ChatRules.h` / `FestivalRules.h` / `WorkState.cpp` / `GameState.cpp` / `PetStateMachine.cpp` / `DesktopEdge.h` / 小游戏插件 | 决定某张立绘**在什么场景被显示** |

**加载入口唯一**（`src/view/PoseLibrary.cpp` L32）：

```cpp
return QStringLiteral(":/poses/%1.webp").arg(QString::fromUtf8(file));
```

> QRC 前缀是 **`/poses/`** 而非 `/assets/poses/`：`assets/assets.qrc` 用 `prefix="/"` + `alias="poses/xxx.webp"` 剥掉了 `assets/` 段（L3–96）。

**利用率的分子/分母**：

- 分母 = L2 登记条目数（= `kPoseCount` = 93 = 磁盘文件数 = qrc 条目数）
- 分子 = **在 L3 层存在至少一条真实可达引用路径**的 pose 数

### 2.2 分类标准（两条正交轴）

**轴 1：文件名前缀分类（现状事实，源自上游命名约定）**

| 前缀 | 数量 | 语义 | 样例 |
|---|---|---|---|
| `dsh-whale-state-<core>` | 29 | 核心状态 / 情绪 / 成长（无类别前缀） | `idle-cute` `sleep` `curious` `levelup` |
| `meme-*` | 18 | 梗表情（关键词命中） | `meme-kyun` `meme-doge` |
| `work-*` | 13 | 工作细分（工具类型 / 关键词） | `work-deploy` `work-debug` |
| `daily-*` | 12 | 日常小剧场（待机随机） | `daily-coffee` `daily-fishing` |
| `weather-*` | 5 | 天气联动 | `weather-umbrella` `weather-snow` |
| `game-*` | 5 | 小游戏四态 + 作弊 | `game-win` `game-lose` |
| `festival-*` | 4 | 节日换装 | `festival-christmas` `festival-spring` |
| `react-*` | 3 | 分区点击反应 | `react-head` `react-belly` |
| **`-peek` 族** | 4 | 贴边探头立绘（**破 `state-` 模板**） | `home-peek` `home-bottom` |
| **前缀违规** | 1 | 节日却无 `festival-` 前缀 | `valentine`（见 R2） |

合计 29+18+13+12+5+5+4+3+4 = **93**。

**轴 2：生命周期状态（本方案新增，落在索引而非磁盘目录）**

| 状态 | 含义 | 预载 | 打包 |
|---|---|---|---|
| `active` | 有真实引用路径 | 按 `preload` 档位 | 进 qrc |
| `lazy` | 无直接引用但**已有明确接入方案** | 仅按需加载并缓存 | 进 qrc |
| `reserved` | 语义上明确「暂不接入」（如天气，见 §五） | 不预载 | **待 owner 决策** |
| `retired` | 确认无价值（参考项目亦弃用） | 不预载 | **待 owner 批准**移出 |

### 2.3 当前使用现状

**（a）55 张有引用的分布**

| 引用来源（L3） | 位置 | 张数 |
|---|---|---|
| 桌面四边贴边 | `src/core/DesktopEdge.h` L27–42 → `PoseView.cpp` L272–306 | 4 |
| 状态机基础姿态 | `src/core/PetStateMachine.cpp`（L14/18/78/84/87/193/222/236–248/262/268/274/280/285/290/294） | 18 |
| 节日换装 | `src/core/FestivalRules.h` L36–41、L66/69/72 | 5 |
| 关键词表情（梗） | `src/core/ChatRules.h` L221–233 | 12 |
| 关键词表情（工作向） | `src/core/ChatRules.h` L227–232 | 8 |
| 工作状态感知 | `src/core/WorkState.cpp` L272–300 | 2 |
| 游戏陪玩 / 小游戏 | `GameState.cpp` L79–91/L127–145；`Minesweeper`/`Kitten`/`Chess` View | 6 |

> 同一张可被多处引用（如 `meme-shock`），故去重后上表之和 = 55。

**（b）38 张零引用（按处置倾向分组，完整清单见附录 A）**

| 分组 | 张数 | 代表 | 性质 |
|---|---|---|---|
| 待机小剧场池候选 | 15 | `daily-eat` `daily-coffee` `daily-stretch` `daily-pajama` `daily-shower` `daily-picnic` `daily-cooking` `daily-fishing` `daily-painting` `cool-shades` `meme-smug` `meme-music` `tail-swing` `daily-done` `wink` | 上游已实现「待机随机池」在用（`IDLE_ACTION_POOL` 14 项 + `wink` 解锁项）→ 我方**能力缺口**，非冗余 |
| 天气联动（明确不做） | 6 | `weather-cold` `weather-rain-happy` `weather-snow` `weather-thunder` `weather-umbrella` `daily-melt` | 与 §五「天气不做」直接相关 → `reserved` |
| 上游有映射、我方未接线 | 12 | `celebrate` `failure` `greet` `night` `running` `sweep` `work-celebrate` `work-idea` `work-pat` `work-slack` `balance-low` `tool` | 上游 `POSES` 已接线（`tool` 上游**复用 `running`**）→ 可选接线 |
| 双方共同死资源 | 5 | `meme-broke` `meme-cry` `meme-heart` `meme-no` `meme-yes` | **参考项目同样零逻辑引用**（仅在其安装器白名单 `scripts/apply-theme.mjs`）→ `retired` 候选 |

> 38 = 15 + 6 + 12 + 5。`tool` 归入「未接线」而非死资源——上游 `whale-moe-core.js` L24 `tool: "running"` 明确说明其已被合并。

**（c）预载与内存现状（`src/view/PoseLibrary.cpp`）**

| 环节 | 现状 | 问题 |
|---|---|---|
| 首批同步 | L42–45：前 `kFirstBatch = 5` 张 | 合理 |
| 后台补齐 | L47–50：**其余 88 张**进队列；L58 每 120 ms 一张 | **含 38 张永不显示** |
| 缓存 | L43 `QHash<QString, QPixmap>` 常驻 | 估算 93 × 256×256×4 B ≈ **23.3 MiB**（其中 ≈ **9.5 MiB** 无效） |
| 失败处理 | L89–92：单张失败仅 `qWarning` 后跳过 | 缺「资源缺失」可观测信号 |

**（d）文档现状**

元数据**只以 Markdown 散文**存在：`PRESENTATION.md` §1（L13–24 分类概览）、`STATE-MACHINE.md` §1（L6–38 语义↔场景，L32–36 **书面承认** `running`/`failure`/`celebrate`/`greet`/`wink`/`night` 无代码路径输出）。
**无机器可读索引**（全仓无 `poses.json` / manifest / registry）。

---

## 三、利用率低下的根本原因

| # | 根因 | 证据 | 后果 |
|---|---|---|---|
| **R1** | **资源冗余：上游「全量继承」而非「按需继承」** | `docs/README.md` §三 决策 #5「复用参考项目 92 张」；实际只接入 55 张的语义 | 38 张（40.9%）零引用，占内存又占包体 |
| **R2** | **命名不规范：模板未成文、存在破例** | ① `valentine` 是节日却无 `festival-` 前缀，其余四张都有；② 4 张 `-peek` 破 `dsh-whale-state-<name>` 模板（`PRESENTATION.md` L8–10 已承认）；③ `kPoses` 表把 4 张 peek 提到表首（`PoseNames.h` L14–16），破坏「同类相邻」 | 无法用前缀可靠枚举类别；新增资源无规律可抄 |
| **R3** | **引用路径混乱：同一资源有两个「权威」表述** | 磁盘 `assets/poses/xxx.webp` vs 运行期 `:/poses/xxx.webp`；`assets.qrc` 的 `alias` 与真实路径**完全相同**（L4–96），alias 无重写作用却制造第二份路径清单 | 排查时两份路径互相干扰（`TRAP-EXT0-003`） |
| **R4** | **缺乏索引：唯一清单是 C++ 源文件，且声称自动生成但生成器未入库** | `PoseNames.h` L3「由 `assets/poses/*.webp` 自动生成，禁止手改」，但仓库内**不存在生成脚本**（无 `tools/`、无 CMake 自定义命令） | 新增姿势须**手工三处同步**；`kPoseCount` 是**独立常量**，中间插入会**静默丢尾项**（`traps-extend0.md` 已记录） |
| **R5** | **缺乏利用机制：没有「让闲置资源被显示」的通路** | 上游有 `IDLE_ACTION_POOL`（14 项待机池）、成长解锁（`wink` 入池）、`TOOL_POSES`（8 条工具→工作姿态）；我方**一条都没有** | 15 张「本就设计给待机池」的资源无处安放，被误读为冗余 |
| **R6** | **缺乏度量与审查：无指标、无门禁、无周期审计** | 无资源校验/体积统计/orphan 检测脚本；覆盖「三方一致」的唯一手段是 `tests/test_state_machine.cpp` L423 的**魔数断言** `QCOMPARE(core::kPoseCount, 93)` | 违规只能在「运行期静默缺图」时暴露；利用率无法度量 |

**收敛**：R1 是表征（零引用存量）；R2/R3/R4 是管理基础设施缺失（无真源索引与命名规范）；R5 是价值转化通路缺失；R6 是反馈闭环缺失。四者叠加使资源池「只进不出、只存不用」。

---

## 四、优化策略

### S1 · 建立统一资源索引（唯一真源 → 解决 R4/R3）

**S1.1 新增 `assets/poses/poses.json` 作为机器可读索引（唯一真源）**

```json
{
  "schemaVersion": 1,
  "poses": [
    {
      "key": "idle-cute",
      "file": "dsh-whale-state-idle-cute",
      "category": "core",
      "status": "active",
      "preload": "core",
      "refs": ["core/PetStateMachine.cpp", "core/WorkState.cpp"],
      "note": "默认待机（对齐参考 idle → idle-cute）"
    },
    {
      "key": "daily-fishing",
      "file": "dsh-whale-state-daily-fishing",
      "category": "daily",
      "status": "lazy",
      "preload": "none",
      "refs": [],
      "note": "待机小剧场池候选，见 S7.1"
    },
    {
      "key": "weather-snow",
      "file": "dsh-whale-state-weather-snow",
      "category": "weather",
      "status": "reserved",
      "preload": "none",
      "refs": [],
      "note": "天气联动；docs/README.md §五 明确不做，保留待决策"
    }
  ]
}
```

| 字段 | 取值 | 说明 |
|---|---|---|
| `key` | `kebab-case` 语义键 | 等价 `PoseEntry::key` |
| `file` | 文件**基名**（无扩展名） | 等价 `PoseEntry::file`；禁止含 `:` 或路径分隔符 |
| `category` | `core`/`meme`/`work`/`daily`/`weather`/`game`/`festival`/`react`/`peek` | 由文件名前缀派生并**双向校验**（S3.2） |
| `status` | `active`/`lazy`/`reserved`/`retired` | 见 §2.2 轴 2 |
| `preload` | `core`/`warm`/`none` | 见 S2 |
| `refs` | 源码相对路径数组 | **由审计脚本自动填充并校验**（只写路径，不写语义） |
| `note` | 自由文本 | `lazy`/`reserved`/`retired` **必须**说明理由 |

**S1.2 新增 `tools/gen_pose_names.py`：由索引生成 `PoseNames.h` 与 qrc 段（补齐「声称自动生成但无生成器」的缺口）**

- 输入 `assets/poses/poses.json` → 输出 `src/core/PoseNames.h`（保持现有 `PoseEntry` / `kPoses[]` 结构；`kPoseCount` 改为 `std::size(kPoses)` 派生量，**消除中间插入丢尾项**）。
- 同时生成 `assets/assets.qrc` 的 `poses/` 段（93 条 `<file alias="...">`），杜绝人工漏登记。
- **生成物入库 + 漂移检测**：不引构建期 Python 依赖（硬约束 2）；改由测试比对「重新生成的文本 == 仓库文件」，不一致即失败。

**S1.3 新增 `tests/test_pose_assets.cpp`：把「铁律」变成「门禁」（→ 解决 R6）**

| 断言 | 内容 | 防的坑 |
|---|---|---|
| A1 | 磁盘 `assets/poses/*.webp` 文件集 == `poses.json` 的 `file` 集 | `TRAP-EXT0-003` |
| A2 | `poses.json` 的 `key/file` 集与顺序 == `PoseNames.h` 的 `kPoses[]` | 同上 |
| A3 | `poses.json` 的 `file` 集 == `assets.qrc` 中 `poses/` 别名集 | 同上 |
| A4 | `category` 与文件名前缀双向一致 | R2 |
| A5 | `file` 命名符合 §S5 模板（含 peek 族例外） | R2 |
| A6 | 每个 `active` 条目至少一条 `refs` 命中真实源码文本 | R1/R6 |
| A7 | 每个 `lazy`/`reserved`/`retired` 条目 `note` 非空 | R5 |
| A8 | `preload=core` 条目数 ≤ 8（防止预载回退为全量） | R1 |
| A9 | `kPoseCount == std::size(kPoses)` | 魔数丢尾项 |

> `test_state_machine.cpp` 现存 `usedPosesExist`（L440）与 `catalogCoversAllPoses`（L420）**保留**，形成「引用侧存在性」与「清单侧一致性」双向覆盖。

### S2 · 预载分档：让预载成本与利用率挂钩（→ 解决 R1，不改行为）

`PoseLibrary` 由「遍历全部 `kPoses`」改为「按索引 `preload` 字段分档」：

| 档位 | 数量（建议） | 时机 | 说明 |
|---|---|---|---|
| `core` | ≤ 6 | 启动同步加载 | 保证首帧（`idle-cute` / `waiting` / `curious` / `thinking` / `success` + 贴边 4 张按需兜底） |
| `warm` | active 剩余 | 启动后 120 ms/张 | 与上游 `POSE_PRELOAD_CORE(5)+EXTRA(16)` 同思路 |
| `none` | `lazy`/`reserved`/`retired` | **不预载**，首次显示时按需加载并缓存 | 依赖既有 `PoseView::loadSourcePixmap()` 兜底路径（L223–243），行为等价 |

**行为等价性保证**：`PoseLibrary::pixmap()` 未命中时调用方统一走 `PoseView::loadSourcePixmap()` 现场加载（已存在），故「不预载」只影响首帧延迟，不影响正确性；`test_smoke` / `test_state_machine` 不受影响（纯逻辑，不依赖预载）。

### S3 · 清理未使用与重复资源（分级处置，不擅自删除）

**S3.1 分级处置表**（对应附录 A 四组）

| 组 | 张数 | 建议处置 | 依据 |
|---|---|---|---|
| 待机池候选 | 15 | **优先接线**（S7.1）→ `active`/`lazy` | 上游已用，属能力缺口 |
| 天气联动 | 6 | `reserved`，不预载；**是否移出 qrc 由 owner 决策** | `docs/README.md` §五 明确不做 |
| 上游有映射未接线 | 12 | 逐张评估：`work-pat`/`work-ram` 可接「忙态分区反应」；`night`/`greet`/`sweep`/`celebrate`/`failure` 可接状态机；`tool` 建议 `retired`（上游已并入 `running`） | 上游 `POSES` L20–48 为直接参照 |
| 双方共同死资源 | 5 | **建议 `retired`**（移出 qrc），须 owner 批准 | 上游亦仅存在于安装器白名单 |

**S3.2 关于「重复资源」的判定口径**

当前 93 张**不存在字节级重复**（无同名/同哈希文件）。所谓「重复」是**语义重复**，例如：

- `sleep`（深夜静息）与 `work-sleep`（工作累了）语义相邻
- `celebrate`（成长庆祝）与 `work-celebrate`（工作完成庆祝）语义相邻
- `running`（活跃）与 `idle-cute`（静息）在忙态区分上有交叠

**处置原则：不合并文件**（立绘是美术产物，合并会破坏语义精度），而是在索引中**显式记录相邻关系**并统一 `category`，由 S1.3-A4 保证分类正确。**判定「是否重复」必须人工目视确认**（见 §六 责任分工），AI 不得据语义猜测删除。

### S4 · 标准化命名与目录结构（→ 解决 R2/R3）

**S4.1 命名模板（成文并机器校验）**

```
dsh-whale-state-<category>-<name>.webp   // 有类别的：meme- / work- / daily- / weather- / game- / festival- / react-
dsh-whale-state-<name>.webp              // core 类（无类别段）
dsh-whale-peek-<scene>.webp              // 贴边探头族（scene ∈ home / bottom / settings / workbench）
```

**S4.2 目录结构：维持扁平 `assets/poses/`（明确取舍）**

- **决策：不引入子目录**。理由：① 93 张规模下子目录收益低于 qrc/alias 与磁盘路径一一对应带来的审查收益；② 分类已由**文件名前缀**承载，`grep` 即检索；③ 与上游 `assets/generated/` 扁平结构对齐，便于资产回流。
- **禁止**在 `assets/poses/` 内放非资源文件（如 `README.md`、`RESERVE.md`）——避免被 qrc/生成器误纳；文档一律落 `docs/`。

**S4.3 需重命名的存量（按成本排序）**

| 项 | 现状 → 目标 | 影响面 | 建议 |
|---|---|---|---|
| `valentine` | `dsh-whale-state-valentine` → `dsh-whale-state-festival-valentine` | `PoseNames.h` + `assets.qrc` + `FestivalRules.h` L72 + 相关测试 | **建议执行**（唯一前缀违规，成本低） |
| 4 张 peek | `dsh-whale-home-peek` → `dsh-whale-peek-home` 等 | `PoseNames.h` + `assets.qrc` + `DesktopEdge.h` L31–37 + `PoseView.cpp` + `STATE-MACHINE.md` §3.1 | **列为可选项**（收益是模板统一，成本牵动 6 处，须 owner 决策） |
| 其余 88 张 | 已合规 | — | 不动 |

> 重命名后**必须**同步 `poses.json` 并跑 S1.3 门禁；`PoseNames.h` 与 qrc 由 S1.2 生成器重生成，人工不改。

### S5 · 引用示例与文档说明（→ 解决 R6 的可读性面）

- **`docs/POSE-ASSETS.md`（本文）= 权威说明**：定义、分类、索引字段、命名模板、指标、流程。
- **索引即文档**：`poses.json` 的 `refs`/`note` 是「哪个场景用哪张」的唯一出处，Markdown 表（`PRESENTATION.md` §1、`STATE-MACHINE.md` §1）改为**引用索引**而非另行维护（避免 `TRAP-P5-003` 式「文档与源不符」）。
- **统一引用范式（写进本文 §S5.1，供代码注释引用）**：

```cpp
// ✅ 正确：通过查表拿文件基名，再由唯一入口拼路径
const char *file = whalepet::core::poseFile("daily-fishing");
// ❌ 禁止：禁止字符串拼接推断立绘名（见 docs/traps-P5.md TRAP-P5-001）
//    auto bad = "dsh-whale-state-" + key;      // 21 项中 11 项会静默退化为 curious
```

- **新增资源 SOP（5 步，写进 `CONTRIBUTING` 等价章节）**：
  1. 美术资源入 `assets/poses/`（256×256、VP8X、webp）；
  2. 在 `poses.json` 追加条目（`key`/`file`/`category`/`status`/`preload`/`note`）；
  3. 跑生成器 `tools/gen_pose_names.py` 重生成 `PoseNames.h` 与 qrc 段；
  4. 首次接入时在业务表登记引用（并更新 `refs`）；
  5. 跑 `test_pose_assets` 门禁 + 全量 CTest。

### S6 · 建立定期审查与更新机制（→ 解决 R6 的闭环面）

| 机制 | 触发点 | 内容 | 产出 |
|---|---|---|---|
| **构建门禁** | 每次 `ctest` | `test_pose_assets`（A1–A9） | 不一致即失败 |
| **变更审查** | 每次新增/改名/退役姿势 | 走 S5 的 5 步 SOP；PR/提交必须含 `poses.json` diff | 索引与代码同变更 |
| **季度审计** | 每季度（或每次阶段收尾） | 跑审计脚本输出 §五 全部指标 + orphan 清单 | 《poses 资源审计报告》（追加至本文附录 C） |
| **退役复核** | 每次审计 | `retired` 项是否可移出 qrc；`reserved` 项是否具备接入条件 | owner 决策记录 |

**审计脚本 `tools/audit_poses.py`（建议交付物）**：输出 ① 逐 pose 引用点清单（自动填 `refs`）；② 零引用清单；③ 体积/尺寸分布；④ 与 `kPoses`/qrc 一致率；⑤ 上述全部指标。

---

## 五、可量化评估指标

**采集方式统一**：全部由 `tools/audit_poses.py` 产出（静态口径），M2/M3 为运行期估算（不含埋点）。基线为 2026-10-03 实测/估算值。

| # | 指标 | 定义（公式） | 基线 | 目标 | 采集 |
|---|---|---|---|---|---|
| **M1** | 引用覆盖率 <br>Reference Coverage | RC = 有真实引用的 pose 数 ÷ 清单内 pose 数 | **59.1%**（55/93） | 阶段 C ≥ 85%；阶段 D ≥ 95%，配合显式退役后 = 100% | 脚本 |
| **M2** | 预载冗余率 <br>Preload Redundancy | PRR = 预载但零引用的 pose 数 ÷ 预载 pose 数 | **40.9%**（38/93） | ✅ **0%**（core 14 + warm 25 全部有真实引用路径；`test_pose_assets::libraryDoesNotPreloadNonTierPoses` 断言） | 单测（索引落地后改由脚本读索引） |
| **M3** | 常驻立绘内存 <br>Resident Pixmap Memory | RSM ≈ Σ(已加载 pose 的 W×H×4 B) | **≈ 23.3 MiB**（93×256²×4） | ✅ **10.0 MiB**（40×256²×4，容量上限 `kCacheCapacity=40`） | ✅ `PoseLibrary::residentBytes()` 埋点 + `test_pose_assets` 断言 |
| **M4** | 复用密度 <br>Reuse Density | RUD = L3 引用点总数 ÷ 活跃 pose 数 | **≈ 1.6**（约 90 点 / 55 张） | 不下降；目标 ≥ 2.0 | 脚本 |
| **M5** | 索引一致率 <br>Index Consistency | IC = (磁盘∩索引∩kPoses∩qrc) 条目数 ÷ 索引条目数 | **N/A**（无索引） | **100%**（硬门禁 A1–A3） | CTest |
| **M6** | 命名合规率 <br>Naming Conformance | NC = 符合 §S4.1 模板的文件数 ÷ 文件总数 | **98.9%**（92/93，`valentine` 违规） | **100%** | 脚本 |
| **M7** | 零引用存量 <br>Orphan Count | ORPHAN = `refs` 为空的 pose 数 | **38** | **0**（全部归入 `active`/`lazy`/`reserved`/`retired` 且 `note` 非空） | 脚本 |
| **M8** | 打包冗余体积 <br>Packaging Redundancy | PRB = `retired`+`reserved` 字节 ÷ `assets/poses` 总字节 | **40.9%**（约 0.80 / 1.96 MiB） | `retired` 全部移出后 ≤ 5% | 脚本 |
| **M9** | 预载命中率 <br>Preload Hit Rate | PHR = 显示时已在缓存中的次数 ÷ 总切换次数 | 未埋点（当前必然 100%，因全量预载） | 保持 ≥ 95%（分档后允许 lazy 首帧现场加载） | 运行期埋点（可选） |
| **M10** | 加载失败率 | 运行期 `[PoseLibrary] 立绘加载失败` 次数 ÷ 总加载次数 | **0%** | ✅ 保持 0% | ✅ `test_pose_assets` 逐张校验 93 张全部通过严格门禁 |
| **M11** | 门禁拦截有效性 | 人为制造违规时 `test_pose_assets` 是否失败 | N/A | 🟡 部分：尺寸/格式/损坏/分档一致性/缓存行为共 18 例已可独立复现失败；**三方一致性（A1–A3）待索引落地** | CTest |

**指标口径说明**：

- M3 的 `W×H×4 B` 是 `QPixmap` 转 ARGB32 后的**理论下限估算**，Windows 上实际实现相关，故标注「≈」，仅用于**横向对比与趋势**，不作为绝对承诺。
- M4 的分母用「活跃 pose 数」而非清单总数，避免退役资源稀释指标。
- M8 中 `reserved` 是否计入取决于 owner 对「移出 qrc」的决策（S3.1）。

---

## 六、分阶段实施步骤

**责任分工**（沿用 `docs/README.md` §六 协作约定）：

| 角色 | 承担 |
|---|---|
| **AI 助手** | 索引/生成器/审计脚本/测试/文档的实现与维护；跑门禁与全量 CTest；给出「已验证」与「推测」的明确标注 |
| **项目 owner（用户）** | 美术取舍决策（是否退役、是否移出 qrc、是否接线）、目视验收、重命名批准；崩溃类问题的调试与根因确认 |

### 阶段 A · 索引与度量落地（**零行为变更**）

| 步骤 | 交付物 | 责任 | 验收 |
|---|---|---|---|
| A1 | `assets/poses/poses.json`（93 条，按 §2.3 现状填写 `status`/`preload`/`note`） | AI | 条目数 = 93 |
| A2 | `tools/gen_pose_names.py` + `tools/audit_poses.py` | AI | 生成结果与仓库 `PoseNames.h`/qrc **逐字节一致**（漂移检测通过） |
| A3 | `tests/test_pose_assets.cpp`（A1–A9 断言）+ 注册进 `cmake/Tests.cmake` | AI | 新增目标全绿；负例可复现失败 |
| A4 | 《poses 资源审计报告》（附录 C 首版），含 M1–M8 基线 | AI | 指标与本文基线一致 |
| A5 | `kPoseCount` 改为 `std::size(kPoses)` 派生量 | AI | 全量 CTest 无回归 |

- **预期效果**：三方同步从「人肉铁律」变为「机器门禁」；利用率首次可度量。
- **风险**：生成器与现有 `PoseNames.h` 的空白/注释格式不一致 → 以「仓库现状为基准反向对齐生成器」为准（不改动既有头文件外观，避免大 diff）。

### 阶段 B · 预载分档与内存回收（**行为等价**）

| 步骤 | 交付物 | 责任 | 验收 |
|---|---|---|---|
| B1 | 索引补齐 `preload` 档位（`core`/`warm`/`none`） | AI | A8 断言通过（`core` ≤ 8） |
| B2 | `PoseLibrary` 改为按档位预载（`core` 同步 → `warm` 定时 → `none` 按需） | AI | `test_smoke` / `test_state_machine` 全绿；贴边、状态切换目视无回归 |
| B3 | 度量常驻内存（M3）与懒加载路径 | AI + owner 目视 | M3 ≤ 10 MiB；贴边立绘首次触发无可见卡顿 |

- **预期效果**：常驻立绘内存 ↓ ≥ 50%（23.3 → ≤ 10 MiB）；启动后台队列由 88 张降到约 50 张。
- **风险**：`none` 档资源首次显示时的现场加载（`PoseView::loadSourcePixmap()`）若在低频路径（如贴边）产生可感知卡顿 → 将 `peek` 族上提为 `warm` 兜底。

### 阶段 C · 命名与分类规范化

| 步骤 | 交付物 | 责任 | 验收 |
|---|---|---|---|
| C1 | 命名模板写入索引校验（A5） | AI | NC = 100%（`valentine` 修正后） |
| C2 | `valentine` → `festival-valentine`（含 `FestivalRules.h` L72 与测试同步） | AI（**须 owner 批准**） | 节日换装 `test_state_machine::festivalCoversAllFiveDays` 全绿 |
| C3 | 4 张 `peek` 归族（可选，成本 6 处）；`kPoses` 排序按 `category` 归并 | owner 决策 → AI 执行 | 目视无回归；A2 顺序断言通过 |
| C4 | `PRESENTATION.md` §1 / `STATE-MACHINE.md` §1 改为引用索引 | AI | 无「文档与源不符」 |

- **预期效果**：NC = 100%；文档与索引单一出处。
- **风险**：C2/C3 属**重命名**，牵动 qrc 与代码字面量 → 必须由生成器统一改写，禁止手工多文件编辑（`TRAP-P5-003` 教训）。

### 阶段 D · 激活闲置资源 + 审查机制常态化

| 步骤 | 交付物 | 责任 | 验收 |
|---|---|---|---|
| D1 | **待机小剧场池**：对齐上游 `IDLE_ACTION_POOL`，接入 ≤ 15 张（`daily-*` / `cool-shades` / `meme-smug` / `meme-music` / `tail-swing` 等） | AI 实现 + owner 目视 | RC 显著上升；静息态不干扰忙态（对齐上游「忙态让位」） |
| D2 | **成长解锁**：`wink` 按等级入池（对齐上游 L1469）、`daily-done` 接完成回报（上游 L1425） | AI + owner | 相关逻辑有单测 |
| D3 | **未接线 12 张逐张决策**：`work-pat`/`work-ram` 接忙态分区反应；`night`/`greet`/`celebrate`/`failure`/`sweep` 接状态机；`tool` 建议 `retired` | owner 决策 → AI 执行 | 决策记录入附录 C |
| D4 | **`reserved` 6 张决策**（天气联动）：维持 `reserved` 并移出 qrc，或接线 | owner 决策 | M8 达标 |
| D5 | **`retired` 5 张**（`meme-broke/cry/heart/no/yes`）处置 | owner 批准 → AI 执行 | 移出 qrc + 索引标记；门禁通过 |
| D6 | 审查机制常态化：门禁 + 季度审计 + 退役复核 | AI | 首份季度报告产出 |

- **预期效果**：RC ≥ 95%，ORPHAN = 0，M8 ≤ 5%。
- **风险**：D1 的随机待机池会改变既有待机观感 → 必须**开关可关**（对齐上游 `whale-moe:*` 偏好开关思路），默认值由 owner 定。

### 里程碑与总体预期效果

| 阶段 | RC | PRR | RSM | NC | IC | ORPHAN |
|---|---|---|---|---|---|---|
| 基线（今日） | 59.1% | 40.9% | ≈23.3 MiB | 98.9% | N/A | 38 |
| A 后 | 59.1%（不变） | 40.9%（不变） | ≈23.3 MiB | 98.9% | **100%** | 38→**0**（已归类） |
| B 后 | 59.1% | **0%** | **≤10 MiB** | 98.9% | 100% | 0 |
| C 后 | ≥ 85% | 0% | ≤10 MiB | **100%** | 100% | 0 |
| D 后 | **≥ 95%** | 0% | ≤10 MiB | 100% | 100% | 0 |

> 说明：A 阶段 ORPHAN 降为 0 的含义是「**每张都有明确归档理由**」，而非「每张都被显示」；真正的显示覆盖率由 D 阶段提升。

---

## 七、与 `dsh-whale-musume` 对标评估

### 7.1 对标结论（先说结论）

**上游不是「高利用率标杆」，而是「同一问题的另一种形态」**：其 92 张立绘同样存在 5 张零逻辑引用（`meme-broke`/`meme-cry`/`meme-heart`/`meme-no`/`meme-yes`，仅出现在安装器白名单 `scripts/apply-theme.mjs`）；
其「立绘数量 = 92」是**文档里的人工 `Measure-Object` 判据**，无自动化校验；`npm test` 不检查任何立绘文件是否存在。

**上游真正强于我方的是三件事**（可直接借鉴）：

1. **分层映射表**让更多资源被真正使用：`POSES`(27) + `IDLE_ACTION_POOL`(14) + `TOOL_POSES`(8) + `KEYWORD_POSES`(21) + `WEATHER_MAP` + 成长解锁 —— 这是上游引用覆盖率显著更高的**唯一原因**，也是我方 15 张「待机池候选」的出处。
2. **两段式素材流**：`output/`（AI 原图 + 审阅切片，`build-review.py` 显式禁止写 `assets/generated`）→ 人工确认 → `build-assets.py`/`slice-batch.py` 入库；`output/` 由 `.gitignore` 排除。**规格常量固化在脚本**（512×512、`padding 10`、WebP lossless q90 m6、绿幕 key `(0,255,0)`）。
3. **分档预载 + 缓存失效约定**：首屏 5 张 → 空闲每 120 ms 一张（`requestIdleCallback` 优先）+ `?v=5` 版本尾巴 + 服务端剥 query。预取失败**完全静默**。

**我方可以超越上游的点**（上游明显短板）：上游**无 manifest**、**无校验脚本**、**无 orphan/体积统计**、**「92」是硬编码魔数**、**规格不统一**（两批资源 39 KB vs 233 KB，差 4–5 倍）、**宿主适配靠源码字符串 `replace`**（脆弱，需单测兜底）。
本方案 S1–S6 正好逐条覆盖这五个短板 —— 因此**不是模仿上游，而是在上游之上补齐工程治理层**。

### 7.2 逐维度对照表

| 维度 | 本项目现状 | `dsh-whale-musume` | 建议 |
|---|---|---|---|
| 资源根目录 | `assets/poses/`（扁平，93 张） | `assets/generated/`（扁平，92 张） | **保持扁平**（对齐上游，见 S4.2） |
| 命名模板 | `dsh-whale-state-<name>.webp` + 4 张 peek 破例 + `valentine` 缺前缀 | 同模板 + 3 张 peek 破例 | 上游同样破例 → **成文为显式命名族**（S4.1）优于「靠约定」 |
| 键值真源 | `PoseNames.h`（C++ 头，**生成器未入库**） | `whale-moe-core.js` 的 `POSES`（JS 常量） | **索引 JSON + 生成器入库**（S1.1/S1.2）—— 上游无此层 |
| 机器可读索引 | **无** | **无** | **新增 `poses.json`**（S1.1）—— 明确超越上游 |
| 一致性校验 | 单测魔数 `kPoseCount == 93` | 仅安装器白名单存在性（bundle 路径不跑）+ 文档人工计数 | **9 条硬断言门禁**（S1.3）—— 明确超越 |
| 元数据（分类/状态） | 散文（`PRESENTATION.md` / `STATE-MACHINE.md`） | 散落在 `POSES`/`IDLE_ACTION_POOL`/`TOOL_POSES`/`KEYWORD_POSES` | **索引承载**，Markdown 改为引用（S5） |
| 预载策略 | **全量 93 张**（40.9% 无效） | 分档（5 core + 16 extra + 其余闲时） | **分档**（S2）—— 对齐上游并挂钩利用率 |
| 闲置资源通路 | **无**（15 张待机池候选无处安放） | 待机池 14 项 + 成长解锁 + 天气/余额 | **移植待机池与解锁**（S7.1/D1/D2）—— 本地化取舍：**不移植天气/余额**（§五 明确不做） |
| 体积/规格治理 | 统一 256×256（无体积分级） | 512×512 统一，但两批体积差 4–5 倍，无分级 | 我方已统一尺寸；可补 **per-场景尺寸分级**（peek 可更小）|
| 缓存失效 | 无版本尾巴（QRC 静态资源，随 exe 更新） | `?v=N` 手动 bump | **不适用**（原生资源随构建更新），无需照搬 |
| 校验脚本 | 无 | 无（`tools/` 只有测试桩与发布工具） | **新增 audit + gen 脚本**（S1.2/S6）—— 明确超越 |
| 废弃流程 | 无（靠事后审计 `traps-extend0.md`） | 有 1 例（删死代码 `ANIM_ROOT` + 加防回归断言） | **索引 `status=retired` + 季度复核**（S3/S6），比上游更体系化 |

### 7.3 可直接借鉴 / 需本地化改造 / 不可照搬

**可直接借鉴**：
1. 待机随机池的**姿态清单本身**（`IDLE_ACTION_POOL` 14 项）—— 我方 15 张候选与之高度重合。
2. **分档预载**与「预取失败静默」的容错姿态。
3. **忙态让位**原则（`BUSY_STATES`）—— 我方 `PRESENTATION.md` §1.1 已对齐，待机池接入时必须遵守。
4. 把「必需资源清单」放进**一个可执行的白名单**（上游 `apply-theme.mjs` 的 `ASSETS`）—— 我方以 `poses.json` 承载，并升级为**双向门禁**。
5. 两段式素材流（`output/` 审阅 → `generated/` 入库）—— 我方若后续新增美术，建立等价 `assets/poses-src/` + `.gitignore` 排除。

**需本地化改造**：
1. 上游的 `?v=N` 缓存失效**不适用**（我方资源由 QRC 编译进可执行文件）。
2. 上游「宿主路由 + 源码 `replace`」的适配方式**不采用**（我方是原生 Qt，无 DOM 注入）。
3. 天气 / 余额联动**明确不做**（`docs/README.md` §五）→ 对应 6 张资源只能 `reserved` 或退役。

**不可照搬（上游的坑）**：
1. **不要把清单写成硬编码魔数**（上游「92」/ 我方 `kPoseCount` 独立常量 → 改为 `std::size` 派生）。
2. **不要用源码字符串 `replace` 做资源路径适配**（脆弱、需单测兜底）。
3. **不要只靠安装器白名单做校验**（bundle 安装路径不跑 → 我方以 CTest 覆盖所有路径）。
4. **不要把「候选图审阅」与「成品目录」混放**（上游用 `build-review.py` 注释显式隔离 → 我方在 S4.2 明确禁止非资源文件进 `assets/poses/`）。

---

## 八、风险、回归防线与不变量

| 风险 | 等级 | 对策 |
|---|---|---|
| 生成器与既有 `PoseNames.h`/qrc 格式不一致，产生巨大噪声 diff | 中 | A2 基准反向对齐；**首版生成结果必须字节一致**才允许进入后续阶段 |
| 分档预载导致低频姿态首帧可感知卡顿 | 中 | `peek` 族归 `warm`；B3 目视验收；保留 `PoseView::loadSourcePixmap()` 兜底 |
| 重命名牵动代码字面量遗漏 | 高 | **只经生成器改写**；A2（顺序）+ A6（引用命中真实源码文本）双门禁 |
| 退役/移出 qrc 属破坏性操作 | **高** | **必须 owner 批准**；分两次提交（先索引标记 `retired`，再移出 qrc）；保留 git 历史可回滚 |
| 待机池改变既有观感 | 中 | 开关可关 + 默认值由 owner 定；忙态让位（对齐 `BUSY_STATES`） |
| 索引与文档再度漂移（`TRAP-P5-003` 重演） | 中 | Markdown 表改为**引用索引**，不再另行维护事实 |
| 崩溃类问题 | — | 按 `docs/README.md` §六：**立即停止并交回用户**，AI 不自行调试 |

**必须长期成立的不变量（写入测试）**：

1. `磁盘文件集 == 索引 file 集 == kPoses.file 集 == qrc poses 别名集`
2. `kPoseCount == std::size(kPoses)`
3. `category` 与文件名前缀双向一致
4. `preload != none` ⇒ `refs` 非空
5. 所有代码引用的 pose key 必存在于索引（`test_state_machine::usedPosesExist` 扩展）

---

## 附录 A · 38 张零引用资源清单与处置建议

（按 §2.3（b）四组排列；`PoseNames.h` 行号供快速定位）

| # | key | 文件 | 分组 | 建议 `status` |
|---|---|---|---|---|
| 1 | `daily-coffee` | `dsh-whale-state-daily-coffee.webp` | 待机池候选 | `active`（D1） |
| 2 | `daily-cooking` | `dsh-whale-state-daily-cooking.webp` | 待机池候选 | `active`（D1） |
| 3 | `daily-done` | `dsh-whale-state-daily-done.webp` | 待机池候选 | `active`（D2） |
| 4 | `daily-eat` | `dsh-whale-state-daily-eat.webp` | 待机池候选 | `active`（D1） |
| 5 | `daily-fishing` | `dsh-whale-state-daily-fishing.webp` | 待机池候选 | `active`（D1） |
| 6 | `daily-painting` | `dsh-whale-state-daily-painting.webp` | 待机池候选 | `active`（D1） |
| 7 | `daily-pajama` | `dsh-whale-state-daily-pajama.webp` | 待机池候选 | `active`（D1） |
| 8 | `daily-picnic` | `dsh-whale-state-daily-picnic.webp` | 待机池候选 | `active`（D1） |
| 9 | `daily-shower` | `dsh-whale-state-daily-shower.webp` | 待机池候选 | `active`（D1） |
| 10 | `daily-stretch` | `dsh-whale-state-daily-stretch.webp` | 待机池候选 | `active`（D1） |
| 11 | `cool-shades` | `dsh-whale-state-cool-shades.webp` | 待机池候选 | `active`（D1） |
| 12 | `meme-smug` | `dsh-whale-state-meme-smug.webp` | 待机池候选 | `active`（D1） |
| 13 | `meme-music` | `dsh-whale-state-meme-music.webp` | 待机池候选 | `active`（D1） |
| 14 | `tail-swing` | `dsh-whale-state-tail-swing.webp` | 待机池候选 | `active`（D1/D2） |
| 15 | `wink` | `dsh-whale-state-wink.webp` | 待机池候选 | `lazy` → `active`（D2 解锁） |
| 16 | `weather-cold` | `dsh-whale-state-weather-cold.webp` | 天气联动 | `reserved` |
| 17 | `weather-rain-happy` | `dsh-whale-state-weather-rain-happy.webp` | 天气联动 | `reserved` |
| 18 | `weather-snow` | `dsh-whale-state-weather-snow.webp` | 天气联动 | `reserved` |
| 19 | `weather-thunder` | `dsh-whale-state-weather-thunder.webp` | 天气联动 | `reserved` |
| 20 | `weather-umbrella` | `dsh-whale-state-weather-umbrella.webp` | 天气联动 | `reserved` |
| 21 | `daily-melt` | `dsh-whale-state-daily-melt.webp` | 天气联动 | `reserved` |
| 22 | `celebrate` | `dsh-whale-state-celebrate.webp` | 未接线 | 待决策（D3） |
| 23 | `failure` | `dsh-whale-state-failure.webp` | 未接线 | 待决策（D3） |
| 24 | `greet` | `dsh-whale-state-greet.webp` | 未接线 | 待决策（D3） |
| 25 | `night` | `dsh-whale-state-night.webp` | 未接线 | 待决策（D3） |
| 26 | `running` | `dsh-whale-state-running.webp` | 未接线 | 待决策（D3） |
| 27 | `sweep` | `dsh-whale-state-sweep.webp` | 未接线 | 待决策（D3） |
| 28 | `work-celebrate` | `dsh-whale-state-work-celebrate.webp` | 未接线 | 待决策（D3） |
| 29 | `work-idea` | `dsh-whale-state-work-idea.webp` | 未接线 | 待决策（D3） |
| 30 | `work-pat` | `dsh-whale-state-work-pat.webp` | 未接线 | `active`（D3 忙态分区） |
| 31 | `work-slack` | `dsh-whale-state-work-slack.webp` | 未接线 | 待决策（D3） |
| 32 | `balance-low` | `dsh-whale-state-balance-low.webp` | 未接线 | `reserved`（余额联动不做） |
| 33 | `tool` | `dsh-whale-state-tool.webp` | 未接线 | **`retired`**（上游已并入 `running`） |
| 34 | `meme-broke` | `dsh-whale-state-meme-broke.webp` | 双方共同死资源 | **`retired`** |
| 35 | `meme-cry` | `dsh-whale-state-meme-cry.webp` | 双方共同死资源 | **`retired`** |
| 36 | `meme-heart` | `dsh-whale-state-meme-heart.webp` | 双方共同死资源 | **`retired`** |
| 37 | `meme-no` | `dsh-whale-state-meme-no.webp` | 双方共同死资源 | **`retired`** |
| 38 | `meme-yes` | `dsh-whale-state-meme-yes.webp` | 双方共同死资源 | **`retired`** |

> `retired` 与 `reserved` 的最终去留**均须 owner 批准**；本表仅为建议，不构成删除授权。

### 附录 A · P8 增量修订（2026-10-04）

P8（`ROADMAP-P8.md`）**不删除任何资产**，但把上表中 17 张从「零引用」变为**有真实代码路径**，
另有 1 张退出输出路径。此后 M7（零引用存量）与 M1（引用覆盖率）应按下表重算：

| 资产 | 原状态 | P8 起 | 引用路径 |
|---|---|---|---|
| `night` | 未接线（待决策 D3） | **`active`** | 傍晚空闲常驻 + 深夜唤醒窗口（`DaySlotRules.h` / `PetStateMachine`） |
| `running` | 未接线（待决策 D3） | **`active`** | 编程族常驻（`WorkPosePool.h::kCodingPose`） |
| `daily-pajama` | 待机池候选（D1） | **`active`** | 深夜空闲常驻（`DaySlotRules.h`） |
| `work-idea` / `work-review` / `work-slack` / `work-slack-phone` / `work-celebrate` / `work-pat` | 未接线（待决策 D3） | **`active`** | 工作立绘池 13 张成员（`WorkPosePool.cpp`） |
| `weather-cold` / `weather-rain-happy` / `weather-snow` / `weather-thunder` / `weather-umbrella` | `reserved`（天气不做） | **`active`** | 预设对话天气题（`WeatherRules.cpp::weatherKindPose`，**免 key 不联网**） |
| `meme-broke` / `meme-cry` / `meme-heart` / `meme-no` / `meme-yes` | `retired` 候选（D5） | **`active`** | 预设对话的敏感 / 私密池与选择池（`DialoguePoseRules.h`） |
| `sleep` | `active`（深夜静息） | **无输出**（按需加载） | P8 起深夜空闲立绘为 `daily-pajama`，`sleep` 无代码路径 |

> 结论：P8 让「天气联动」与「5 张双方共同死资源」这两组此前判定为 `reserved`/`retired` 的资产
> **获得了真实用途**（需求的直接结果），故附录 C 的 M1/M7 口径应随之更新；
> D4（天气 6 张）与 D5（死资源 5 张）的决策项相应关闭。

> 第 1–15 张的大小与分类与上游 `IDLE_ACTION_POOL`（`daily-eat` `daily-coffee` `daily-stretch` `daily-pajama` `daily-shower` `cool-shades` `meme-smug` `daily-picnic` `daily-cooking` `daily-fishing` `daily-painting` `daily-gaming` `tail-swing` `meme-music`）高度重合，可直接作为 D1 的初始池。

---

## 附录 B · 命名规范速查表

```
✓ dsh-whale-state-idle-cute.webp          core 类，无类别段
✓ dsh-whale-state-festival-christmas.webp 有类别段
✓ dsh-whale-state-meme-kyun.webp
✓ dsh-whale-peek-home.webp                peek 族（S4.1 定义，现为 dsh-whale-home-peek.webp）
✗ dsh-whale-state-valentine.webp          节日缺 festival- 前缀（S4.3 待修）
✗ dsh-whale-home-peek.webp                破 state- 模板（S4.3 可选项）
✗ daily_coffee.webp                       禁止下划线
✗ dsh-whale-state-DailyCoffee.webp        禁止驼峰
```

**命名规则**：全小写 `kebab-case`；仅 `[a-z0-9-]`；必有 `dsh-whale-` 前缀；以 `.webp` 结尾；`category` 段必须与文件名前缀一致。

---

## 附录 C · 审计记录（随阶段追加）

| 日期 | 阶段 | RC | PRR | RSM | NC | IC | ORPHAN | 备注 |
|---|---|---|---|---|---|---|---|---|
| 2026-10-03 | 基线（本文调研） | 59.1% | 40.9% | ≈23.3 MiB | 98.9% | N/A | 38 | M3 为估算值，非实测 |
| 2026-10-04 | **B（路径 A）落地** | 59.1%（不变） | **0%** | **9.0 MiB** | 98.9% | N/A | 38 | M2/M3 由 `test_pose_assets` 断言；M3 为 `residentBytes()` 埋点值（ARGB32 理论下限口径）。ORPHAN 仍为 38：路径 A 只改**加载策略**，不改变引用，38 张零引用立绘现按需加载而非删除——处置仍待 owner 决策。Debug / Release CTest 各 **32/32** |
| 2026-10-04 | **P8（时段 / 工作池 / 预设对话）** | **≈77.4%**（72/93，见下注） | **0%** | **10.0 MiB**（capacity 40） | 98.9% | N/A | **≈21** | P8 让 17 张（`night`/`running`/`daily-pajama`/6×`work-*`/5×`weather-*`/5×`meme-*`）进入真实引用路径（见「附录 A · P8 增量修订」），`sleep` 退出输出；档位 core **14** + warm **25**（=39 < 40），预载不互逐。Debug CTest **33/33**（新增 `test_preset_dialogue`） |
| — | A2/A5 完成后填写 | | | | | | | |
| — | C 完成后填写 | | | | | | | |
| — | D 完成后填写 | | | | | | | |

> 本表由季度审计追加；每行须标注「实测」或「估算」，禁止以推测值冒充实测。

