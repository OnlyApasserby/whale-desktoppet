# ROADMAP · P2 — 表现层与状态机

> ✅ 已完成并改签为 `ROADMAP-P2-Fin.md`（2026-09-30 人工复验通过，9 项中 8 项通过；
> `TRAP-P2-007` 为未复现的长期观察项，不阻塞）。

## 阶段目标

移植 whale 状态机为纯 C++ 逻辑，并接入全部立绘与程序化动效，让桌宠「活」起来。

## 交付物

- `core/PetStateMachine`（纯逻辑，可注入 RNG，可单测）。
- `viewmodel/PosePresenter`（语义结果 → Qt 动效/气泡）。
- 92 张立绘接入 + 惰性预载。
- 程序化动效：呼吸、拖拽摇摆/惯性、状态过渡遮断、点击反馈、特效。

## 任务清单

1. 移植 `core.js` 状态集合、时间窗口常量、转移规则到 C++。
2. 定义 `PoseResult{pose, lineKey, fx, ttlMs}` 输出结构。
3. 接入 pose→立绘路径映射（同名 webp）。
4. 实现动效：
   - 待机呼吸缩放；
   - 拖拽按位移方向摇摆 + 松手惯性滑行 + 旋转回正；
   - 「下压→换图→弹起」过渡遮断；
   - 单击分区反馈（head/belly/tail）、三连击（star + 粒子）。
5. 台词气泡窗口（跟随立绘，自动消失）。
6. 立绘惰性预载（首屏 5 张，其余 ~120ms/张，失败静默）。
7. 冒烟单测：`PetStateMachine` 转移 / 超时回落 / 节流。

## 验收标准

- [ ] 各事件触发的姿态切换正确，无叠影/闪黑。
- [ ] 拖拽摇摆与惯性手感自然（对比 whale 表现）。
- [ ] 分区点击 / 三连击有专属立绘 + 特效。
- [ ] 状态机单测通过，且脱 UI 可运行。
- [ ] 空闲 CPU 仍 < 5%。

## 依赖

- P1（外壳、立绘显示）。

---

# P2 增补（首次目视验收后的修正）

> **背景**：首次目视验收结论 —— 第 **1 / 2 / 3 / 5 / 7** 项确认无误；第 **4**（三连击迸发）、第 **6**（台词切换）需修正；
> 另有验收范围外新增问题：**右键菜单被立绘抢占焦点**（菜单中「夸夸」的特效与第 4 项同因）。
>
> 本节**只写方案与待确认问题**，用户确认后再实施；**在确认前不修改任何代码**。
>
> 关联：`docs/PRESENTATION.md`（动效/气泡）、`docs/STATE-MACHINE.md`（台词节流）、`docs/pitfalls/`（根因将回填为踩坑条目）。

## 一、根因定位（已核对代码，非推测）

### A. 特效「过于频繁」（第 4 项；「夸夸」同因）

证据链：

1. `PetController::onTick()` 每 `core::kTickMs`（**200ms**）都会调用两次 `present()`：
   ```cpp
   presentCurrent();                                      // present(m_sm.current())
   m_presenter->present(m_sm.handle(core::Event::tick(nowMs())));
   ```
2. `PosePresenter::present()` 只要 `result.fx != core::Fx::None` 就**无条件**调用 `m_view->playFx(result.fx)`。
3. 一次性姿态在存续期内，`PetStateMachine::m_current`（被 `current()` 返回的缓存态）**始终携带同一个 `fx`**，
   于是同一次操作的特效被**按 tick 反复播放**：

| 事件 | fx | 存续时长 | 实际触发次数 | 粒子量（约） |
|---|---|---|---|---|
| 三连击（`TripleClick`） | `Fx::Particle` | `kSuccessWindowMs` = 2s | ≈ 10 次 × `kSparkCount` 26 | ≈ 260（被 `kMaxParticles`=48 截断，但每 200ms 继续补） |
| 夸夸（`Praise`） | `Fx::Heart` | `kCuriousWindowMs` = 6s | ≈ 30 次 × `kHeartCount` 6 | ≈ 180 |

结论：现象是「**一次操作 → 持续 2–6s 的连续迸发**」，而不是频率偏高。**只加冷却不足以修正次数语义。**

### B. 台词「触发切换过于频繁」（第 6 项）

三处叠加（前两处是 bug，第三处是语义冲突）：

1. **缓存态重放**：每 tick 的 `present(m_sm.current())` 携带着缓存态里的 `lineKey`，
   而 `PosePresenter` 每次都 `m_lines->pick(...)` **重新随机取一条** → 每 200ms 换一句，同一句也不会重复。
2. **节流失效**：`PetStateMachine::makeLine()` 里的 `canSpeak()`（`kSpeechGapMs` = 6s）只在**生成 `lineKey` 那一刻**生效，
   对「缓存态被反复重放」没有任何约束 → 节流被绕过。
3. **与验收要求直接冲突**：用户交互（`proactive = false`）与主动说话共用同一节流额度，
   因此「连点不同部位」时只有**第一次**点击能拿到台词，第 2/3 次点击 `lineKey` 为空。
   → 现状是「只显示**第一次**操作的台词」，而要求是「只显示**最后一次**操作的台词」。

### C. 右键菜单被立绘抢占焦点（验收范围外）

- `PetWindow` 的窗口标志为 `Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool`；
- `QMenu::exec()` 弹出的菜单窗口类型为 `Qt::Popup`；
- 两者在 Z 序上的关系使置顶的立绘压在菜单之上 → 菜单被遮挡，或点击落到立绘上。

## 二、修改方案

### 0. 共同基础：「表现批次序号」去重令牌（解决 A/B 的重放问题）

在 `core::PoseResult` 增加两个自增序号，语义为「**这是新的一次表现**」：

```cpp
struct PoseResult {
    std::string pose;
    std::string lineKey;
    Fx fx = Fx::None;
    int ttlMs = 0;
    std::uint32_t fxSerial = 0;    // fx != None 时递增；同一结果重放时不变
    std::uint32_t lineSerial = 0;  // lineKey 非空时递增；同一结果重放时不变
};
```

- 递增点：`PetStateMachine::applyOneShot()` 内——仅在**新事件产生新结果**时自增；
  `handle(Tick)` 复用 `m_current`、`fallback()` 回落（`fx=None`、`lineKey=""`）时**不递增**。
- `PosePresenter` 持有 `m_lastFxSerial` / `m_lastLineSerial`，**仅当序号变化**才 `playFx()` / `showLine()`。
  → 「每 tick 重推同一结果」天然只表现一次，且不需要在 View 层猜测"是不是重复"。
- 该去重是**幂等的语义层修正**，不依赖时钟，可脱 UI 单测（连续 present 同一结果 N 次，断言只表现 1 次）。

### 1. 特效：一次操作一次迸发 + 500ms 强制间隔（方案 A）

1. **次数语义**：由 §0 的 `fxSerial` 保证「点击一次只触发一次迸发」。
2. **频率下限**：`PoseView::playFx()` 增加强制最小间隔（新常量置于 `src/common/PetVisuals.h`）：
   ```cpp
   inline constexpr int kFxMinGapMs = 500;   // 强制特效触发间隔
   ```
   实现：记录 `m_lastFxMs`；`now - m_lastFxMs < kFxMinGapMs` 时**丢弃**本次触发（不排队、不延迟）。
3. **避免叠影**：同类粒子再次触发时，先移除该 `kind` 的残留粒子，再生成新的一批（避免「旧的一批还没消散 + 新的一批」看起来仍在连续迸发）。
4. **不受限项**：`PoseView::clickFeedback()`（立绘下压/上顶的点击手感）**不纳入** 0.5s 限制，否则连点无反馈。
5. 夸夸（`Fx::Heart`）与三连击（`Fx::Particle`）**共用**同一冷却（见待确认问题 Q1）。

### 2. 台词：最后一次为准 + 流式输出（方案 B）

1. **只播一次**：由 §0 的 `lineSerial` 保证「一次操作只播放一次台词」。
2. **最后一次为准**：`PosePresenter::showLine()` 采用**打断式**语义——
   新台词到达时**立即中止**当前台词的流式输出，清空并从第一个字重新开始；不做队列、不拼接。
   → 连点不同部位时，最终停留在最后一次操作的台词上。
3. **节流范围调整**：`PetStateMachine::makeLine()` 的 6s 节流**只约束主动说话**（`proactive = true`，如待机小剧场的「逗弄」），
   **用户交互台词（`proactive = false`）不再受 6s 约束**——否则「最后一次为准」在语义上无法成立。
   深夜静默 / 面板打开静默（`m_suppressed`）仍只作用于主动说话，用户交互照旧可回应。
4. **流式输出**：`SpeechBubble` 由「一次性 `setText` + 定时隐藏」改为「逐字揭示」：
   - 新增 `startStream(const QString &text, int ttlMs)`；内部用 `QTimer` 按字符间隔推进，逐字追加到 `QLabel`。
   - 新常量（`src/common/PetVisuals.h`）：
     ```cpp
     inline constexpr int kStreamCharIntervalMs = 40;   // 每字间隔 ≈ 25 字/秒
     inline constexpr int kStreamPunctPauseMs = 120;    // 标点额外停顿（，。！？…）
     ```
   - **TTL 语义**：隐藏计时从**流式结束后**起算（`总时长 = 打字时长 + ttlMs`），避免长句没打完就消失。
   - **气泡尺寸**：按**整句**预排版确定气泡尺寸，打字过程中尺寸保持不变（避免逐字 `adjustSize()` 造成气泡与立绘的抖动）。
   - **位置**：流式中立绘移动 → 继续调用现有 `reposition()`（已有实现，无需改动）。
5. 台词仍然**只由 `PosePresenter` 触发**，`SpeechBubble` 自身不感知状态机语义（保持分层）。

### 3. 右键菜单：强制置顶（方案 C）

1. **首选**：菜单窗口强制置顶 + 抬升
   - `setupContextMenu()` 中对 `m_menu` 设置 `Qt::WindowStaysOnTopHint`；
   - 在 `exec()` 前 `raise()`，并通过事件过滤器在 `QMenu` 的 `Show` 事件里再 `raise()` 一次（Windows 上 `Popup` 与 `StaysOnTop` 的组合存在时序差异，需实测）。
2. **备选**（若首选仍被遮挡）：菜单打开期间**临时取消立绘置顶**，关闭后恢复
   - 用 `QMenu::aboutToShow` / `aboutToHide` 切换 `Qt::WindowStaysOnTopHint`（需 `show()` 重建窗口标志，注意闪烁，需实测观感）。
3. **菜单期间屏蔽立绘鼠标**：新增 `m_menuOpen` 标志，在菜单打开期间 `PetWindow::mousePressEvent` / `contextMenuEvent` 直接 return，
   避免「点菜单项点到立绘」（是否需要取决于待确认问题 Q10 的实际现象）。
4. **托盘菜单同样处理**（`m_tray` 的 `QMenu` 是独立实例，需一并置顶）。

## 三、待确认问题（Open Questions）—— **已确认**

> **决议（用户已确认）**：**全部按建议默认值实施**；
> **Q10 答复：现象为 (a)「菜单被立绘遮住」** → 只做**菜单置顶**（方案 C-1），
> **不**实现方案 C-3（菜单期间屏蔽立绘鼠标事件）：既然不存在点击穿透，屏蔽鼠标属于无依据的改动。
>
> 下表保留原始问题与其建议默认值，作为决议依据。

> 标 **【建议默认】** 的项，若无需特别指定，将按建议值实施。

**特效（第 4 项 / 夸夸）**

- **Q1** 500ms 强制间隔是「所有特效共用一个冷却」还是「按特效类型各自独立计时」？**【建议默认：所有特效共用】**
- **Q2** 冷却期内的触发是**直接丢弃**，还是**排队延迟到冷却结束再播**？**【建议默认：直接丢弃】**
- **Q3** 「点击反馈」（立绘下压/上顶，非粒子）是否也纳入 500ms 限制？**【建议默认：不纳入，保留连点手感】**

**台词（第 6 项）**

- **Q4** 确认**取消用户交互台词的 6s 节流**（节流仅保留给主动说话）？
  此项会推翻既有设计，实施时需同步改写单测 `StateMachineTest::speechIsThrottled` 与 `docs/STATE-MACHINE.md`。**【建议默认：取消（仅约束主动说话）】**
- **Q5** 三连击的第 3 下会**同时**产生 `Click` 台词与 `TripleClick` 台词：按「最后一次为准」只显示 `TripleClick` 的台词（`click.triple`）？**【建议默认：只显示 TripleClick 的】**
- **Q6** 流式输出的节奏：每字 40ms（≈25 字/秒）+ 标点额外停顿 120ms，是否合适？**【建议默认：40ms / 120ms】**
- **Q7** 隐藏计时从**流式结束后**起算（长句不会被打断消失），还是从出现即起算？**【建议默认：流式结束后起算】**
- **Q8** 打字过程中气泡尺寸保持**整句预排版后的固定尺寸**（不随字数变化）？**【建议默认：固定尺寸】**
- **Q9** 是否需要「打字中点击立绘 → 立即显示全文（跳过打字）」？
  注意：点击本身又会触发**新台词**（打断并重打），两者语义冲突。**【建议默认：不做跳过，点击一律视为新操作】**

**右键菜单**

- **Q10**（**必须确认**）「被抢占焦点」的确切现象是哪一种？
  - (a) 菜单**被立绘遮住**（视觉问题，只需置顶）；
  - (b) 菜单可见但**点击菜单项无效 / 点到立绘**（需要同时屏蔽立绘鼠标事件）；
  - (c) 两者都有。
  该答案直接决定「是否只需要方案 C-1」还是「必须叠加 C-3」。
- **Q11** 托盘菜单是否也按同一方案置顶？**【建议默认：是】**

**分层与范围**

- **Q12** 是否允许改动 `core/`（即 `PoseResult` 增加 `fxSerial` / `lineSerial`）？
  这是「表现批次去重」最干净的位置，但会牵动 `test_state_machine` 与 `STATE-MACHINE.md`。
  若不允许，则退化为在 `PosePresenter` 内自行比对上一次结果（脏、且难以覆盖 Tick 与事件同帧的边界）。**【建议默认：允许改 core】**

## 四、影响面（实施时同步修改）

| 文件 | 改动 |
|---|---|
| `src/core/PetTypes.h` | `PoseResult` 增加 `fxSerial` / `lineSerial`（Q12 通过后） |
| `src/core/PetStateMachine.h/.cpp` | 序号递增；`makeLine()` 节流仅作用于主动说话（Q4） |
| `src/viewmodel/PosePresenter.h/.cpp` | 按序号去重；无新台词时不打断当前流式 |
| `src/view/PoseView.h/.cpp` | `playFx` 强制 500ms 间隔 + 同类粒子清理 |
| `src/view/SpeechBubble.h/.cpp` | 流式输出（逐字揭示、打断式、TTL 后置） |
| `src/view/PetWindow.cpp` | 菜单置顶（右键 + 托盘）；**不做**菜单期间事件屏蔽（Q10 = (a)） |
| `src/common/PetVisuals.h` | `kFxMinGapMs` / `kStreamCharIntervalMs` / `kStreamPunctPauseMs` |
| `tests/test_state_machine.cpp` | `speechIsThrottled` 按新语义改写；新增序号语义用例 |
| `tests/`（新增或扩展） | 「同一结果重复 present → 只播一次特效 / 只播一次台词」用例（需可注入的假 View / 假 Bubble 或 Qt spy） |
| `docs/STATE-MACHINE.md` | 节流范围（用户交互 vs 主动说话）同步 |
| `docs/PRESENTATION.md` | 特效间隔、流式输出、气泡尺寸策略同步 |
| `docs/pitfalls/` | 回填「缓存态重放导致 fx/台词重复触发」与「菜单被置顶立绘遮挡」两条 |

## 五、本轮增补的验收标准

**实现状态：已全部实现并通过自动化验证 + 人工目视复验（2026-09-30）。**

- [x] 三连击：一次触发**恰好一次**迸发；500ms 内重复触发被丢弃。
- [x] 夸夸：一次触发**恰好一次**爱心上浮（不再持续 6s）。
- [x] 点击反馈（下压/上顶）不受 500ms 限制，连点仍有手感。
- [x] 连续点击不同部位：**只显示最后一次**点击部位对应的台词；不出现「同一句/换句被反复重放」。
- [x] 台词逐字出现；新台词**立即打断**并从第一个字重新开始。
- [x] 长句能完整显示完再消失（TTL 后置生效）。
- [x] 右键菜单完整可见且可正常点击，不被立绘遮挡（含托盘菜单）。
- [x] Debug / Release 构建通过；CTest 全绿（含新增去重用例）。

## 六、实施记录（本轮增补）

| 项 | 落地内容 |
|---|---|
| 序号去重 | `PoseResult` 增 `fxSerial`/`lineSerial`；状态机统一走 `compose()` 分配序号（重放/回落不自增，`reset()` 不清零）；`PosePresenter` 按序号播放 |
| 特效间隔 | `PoseView::playFx()` 加 `kFxMinGapMs` = 500ms 共用冷却 + 同类粒子清理；点击反馈不受限 |
| 台词节流 | `makeLine()` 改为**只约束主动说话**，用户交互不节流也不消耗额度 |
| 流式输出 | `SpeechBubble::startStream()`：40ms/字 + 标点 +120ms、打断式重打、TTL 后置、整句预排版固定尺寸；`showLine()` 保留为非流式路径 |
| 菜单层级 | 右键菜单与托盘菜单均 `WindowStaysOnTopHint` + `Show` 事件 `raise()`；未做鼠标屏蔽 |
| 单测 | `test_state_machine`：`speechThrottleAppliesToProactiveOnly`（改写）、`serialsMarkOnlyNewOutcomes`（新增）；`test_smoke`：`fxSerialPlaysOnceAndRespectsGap`、`lineSerialDedupesAndStreamInterrupts`（新增） |
| 验证 | Debug / Release 构建通过；CTest Debug **3/3**、Release **3/3**（`test_smoke` 6 个用例全过） |
| 踩坑 | `docs/pitfalls/` 新增 `TRAP-P2-009`（缓存态重放）、`TRAP-P2-010`（菜单被遮挡） |

> 语义细节（已固化为约定）：被 500ms 间隔丢弃的特效**不会补播**——
> `PosePresenter` 在提交前即消费序号，避免「排队」变成延迟播放（Q2 = 直接丢弃）。

## 七、收尾复核

> 按 `README.md` §二.2，`Fin` 只在**全部验收通过**后追加。本轮先由 AI 把「可自动化」的部分重新跑了一遍并固化结论；
> 「需真实桌面目视」的部分交由用户在真实桌面复验，**复验结果见下方「用户人工复验结果」**（已全部通过，本文件已改签 `Fin`）。

### 复核结果（2026-09-30，本机）

| 项 | 命令 | 结果 |
|---|---|---|
| configure | `cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"` | 通过 |
| Debug 构建 | `cmake --build build --config Debug --parallel` | 通过 |
| Release 构建 | `cmake --build build --config Release --parallel` | 通过 |
| Debug CTest | `ctest --test-dir build -C Debug --output-on-failure --timeout 120` | **3/3 Passed**（2.0s） |
| Release CTest | `ctest --test-dir build -C Release --output-on-failure --timeout 120` | **3/3 Passed**（1.9s） |

> 运行 CTest 前须注入 Qt `bin`（`TRAP-P2-008`）并设 `QT_QPA_PLATFORM=offscreen`。

### 用户人工复验结果（2026-09-30，真实桌面）

| # | 复验项 | 结果 | 实测记录 |
|---|---|---|---|
| 1 | 三连击 | ✅ 通过 | 一次触发恰好一次迸发，500ms 内连点被丢弃、不补播 |
| 2 | 夸夸 | ✅ 通过 | 一次触发恰好一次爱心上浮 |
| 3 | 点击反馈 | ✅ 通过 | 下压/上顶不受 500ms 限制，连点手感正常 |
| 4 | 连点不同部位 | ✅ 通过 | 只显示最后一次点击部位的台词，无重放/刷屏 |
| 5 | 台词流式 | ✅ 通过 | 逐字出现，新台词立即打断，长句完整显示后消失 |
| 6 | 菜单层级 | ✅ 通过 | 右键菜单与托盘菜单均完整可见、可点击 |
| 7 | 动效总观感 | ✅ 通过 | 姿态切换无叠影/闪黑，拖拽摇摆与惯用手感自然 |
| 8 | 空闲占用 | ✅ 通过 | **空闲 0.0%–0.1% CPU；拖动 0.3%–0.4% CPU**（判据 < 5%） |
| 9 | 偶发崩溃 | ⏳ 长期观察 | 挂机期间**仍未观测到** `TRAP-P2-007`；该条为未定位观察项，不阻塞收尾 |

**合计：9 项中 8 项通过、1 项（`TRAP-P2-007`）维持未观测的长期观察状态。**

### 结论

- P2 的**代码实现、自动化验证与人工目视验收均已完成**。
- 遗留项仅有 `TRAP-P2-007`（AI 侧 offscreen 偶发 `0xC0000409`，用户侧多轮复验未复现），
  按 `README.md` §六 作为**长期观察项**保留，**不阻塞**本阶段收尾。
- 故本文件改签为 `ROADMAP-P2-Fin.md`；若后续复现该崩溃，按 `docs/pitfalls/` 的取证步骤回填。

## 完成标记

✅ **已完成** —— 2026-09-30 人工复验通过，本文件改签为 `ROADMAP-P2-Fin.md`。
