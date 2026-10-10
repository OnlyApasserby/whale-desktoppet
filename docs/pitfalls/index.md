# WhalePet · 踩坑记录索引

> **唯一入口**：本文件。`docs/README.md` §七 指向此处。
> **存放规则**：按实施阶段分文件夹（`p1/` … `p8/`、`p9/`、`ext0/`、`ex1/`、`ex2/`、`ex3/`、`ex4/`、`ex5/`）；每个真实问题一份文件，命名 `P-<三位序号>-<短横线短语>.md`，序号**全局单调递增、不复用、不重排**。
> **原始编号**：条目内保留历史编号 `TRAP-<阶段>-<序号>`（如 `TRAP-P7-011`），用于与既有文档、ROADMAP 与提交记录交叉引用。
> **新增条目**：新建 `P-<三位序号>-<短横线短语>.md` 放进对应阶段文件夹，并在下方「按阶段索引」补一行。
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **迁移说明**：原按阶段聚合的 `docs/traps-P1.md` … `traps-P8.md`、`traps-extend0.md`、`traps-ex1.md`（共 10 份）已拆分为 80 份独立条目文件，**正文未改动**。

| 阶段 | 文件夹 | 条目数 | 序号区间 |
|---|---|---|---|
| P1 | [`p1/`](p1/) | 5 | `P-001` … `P-005` |
| P2 | [`p2/`](p2/) | 12 | `P-006` … `P-017` |
| P3 | [`p3/`](p3/) | 5 | `P-018` … `P-022` |
| P4 | [`p4/`](p4/) | 6 | `P-023` … `P-028` |
| P5 | [`p5/`](p5/) | 3 | `P-029` … `P-031` |
| P6 | [`p6/`](p6/) | 7 | `P-032` … `P-038` |
| P7 | [`p7/`](p7/) | 20 | `P-039` … `P-058` |
| P8 | [`p8/`](p8/) | 10 | `P-059` … `P-068` |
| EXT0 | [`ext0/`](ext0/) | 5 | `P-069` … `P-073` |
| EX1 | [`ex1/`](ex1/) | 12 | `P-074` … `P-080`、`P-087` … `P-091` |
| P9 | [`p9/`](p9/) | 4 | `P-081` … `P-084` |
| EX2 | [`ex2/`](ex2/) | 2 | `P-085` … `P-086` |
| EX3 | [`ex3/`](ex3/) | 2 | `P-092` … `P-093` |
| EX4 | [`ex4/`](ex4/) | 7 | `P-094` … `P-100` |
| EX5 | [`ex5/`](ex5/) | 3 | `P-101` … `P-103` |
| **合计** |  | **103** |  |

## 一、按阶段索引

### P1 · traps · P1 — 工程骨架与桌宠外壳（真实踩坑记录）（5 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-001** | `TRAP-P1-001` | `Q_INIT_RESOURCE` 放入命名空间导致 LNK2019 | [`P-001-q-init-resource-namespace-lnk2019.md`](p1/P-001-q-init-resource-namespace-lnk2019.md) |
| **P-002** | `TRAP-P1-002` | `qt_standard_project_setup()` 不启用 `AUTORCC`，`.qrc` 被静默忽略 | [`P-002-autorrcc-not-enabled-qrc-ignored.md`](p1/P-002-autorrcc-not-enabled-qrc-ignored.md) |
| **P-003** | `TRAP-P1-003` | `windeployqt --dir` 不复制 exe 本体，部署目录缺主程序 | [`P-003-windeployqt-dir-missing-exe.md`](p1/P-003-windeployqt-dir-missing-exe.md) |
| **P-004** | `TRAP-P1-004` | PowerShell 管道吞掉退出码，构建失败被误判为成功 | [`P-004-powershell-pipeline-swallows-exit-code.md`](p1/P-004-powershell-pipeline-swallows-exit-code.md) |
| **P-005** | `TRAP-P1-005` | 进程内存单次读数抖动（28MB → 76MB），易误判为超阈值 / 泄漏 | [`P-005-memory-sample-jitter.md`](p1/P-005-memory-sample-jitter.md) |

### P2 · traps · P2 — 立绘/动效/交互接线（真实踩坑记录）（12 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-006** | `TRAP-P2-001` | `Event::keyword` 工厂函数与同名字段冲突（C2365） | [`P-006-event-keyword-factory-name-clash-c2365.md`](p2/P-006-event-keyword-factory-name-clash-c2365.md) |
| **P-007** | `TRAP-P2-002` | 测试里 `core::` 无法解析（C2653） | [`P-007-test-core-namespace-c2653.md`](p2/P-007-test-core-namespace-c2653.md) |
| **P-008** | `TRAP-P2-003` | 无控制台环境下 QTest 结果「消失」 | [`P-008-qtest-headless-output-lost.md`](p2/P-008-qtest-headless-output-lost.md) |
| **P-009** | `TRAP-P2-004` | 一次性姿态到期断言写错，误判状态机有 bug | [`P-009-oneshot-pose-expiry-assert-wrong.md`](p2/P-009-oneshot-pose-expiry-assert-wrong.md) |
| **P-010** | `TRAP-P2-005` | 立绘路径拼装方式对 `*-peek` 不通用 | [`P-010-pose-path-concat-peek.md`](p2/P-010-pose-path-concat-peek.md) |
| **P-011** | `TRAP-P2-006` | 过渡遮断在「起点」换图，动效退化成「先换图再动画」 | [`P-011-transition-swap-at-start-frame.md`](p2/P-011-transition-swap-at-start-frame.md) |
| **P-012** | `TRAP-P2-007` | Release 部署版偶发 `0xC0000409`（**未定位，暂缓观察**） | [`P-012-release-0xc0000409-unlocated.md`](p2/P-012-release-0xc0000409-unlocated.md) |
| **P-013** | `TRAP-P2-008` | 未注入 Qt `bin` 时 CTest 全部 `0xC0000135`，形似崩溃 | [`P-013-test-0xc0000135-qt-path.md`](p2/P-013-test-0xc0000135-qt-path.md) |
| **P-014** | `TRAP-P2-009` | 状态机每 tick 重推缓存结果，下游「按字段判断」导致特效连播、台词狂换 | [`P-014-state-tick-refire-suppression.md`](p2/P-014-state-tick-refire-suppression.md) |
| **P-015** | `TRAP-P2-010` | 置顶立绘压住无边框 `QMenu`，右键菜单被遮挡 | [`P-015-topmost-overlay-blocks-qmenu.md`](p2/P-015-topmost-overlay-blocks-qmenu.md) |
| **P-016** | `TRAP-P2-011` | `TRAP-P2-010` 修复不彻底：立绘压住菜单 + 桌宠抢焦点 | [`P-016-topmost-qmenu-fix-incomplete.md`](p2/P-016-topmost-qmenu-fix-incomplete.md) |
| **P-017** | `TRAP-P2-012` | `#SpeechBubble` 圆角背景框规则存在却**从未绘制**（气泡只有字没有底） | [`P-017-speechbubble-background-never-painted.md`](p2/P-017-speechbubble-background-never-painted.md) |

### P3 · traps · P3 — 养成与数据层（真实踩坑记录）（5 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-018** | `TRAP-P3-001` | `expSpanForLevel()` 的等级夹取退化 | [`P-018-expspan-level-clamp.md`](p3/P-018-expspan-level-clamp.md) |
| **P-019** | `TRAP-P3-002` | `Schema::migrate()` 无法接收按值返回的 `QSqlDatabase` | [`P-019-schema-migrate-qsqldatabase-byvalue.md`](p3/P-019-schema-migrate-qsqldatabase-byvalue.md) |
| **P-020** | `TRAP-P3-003` | 养成数据目录会出现在 exe 同级 `data/` | [`P-020-data-dir-next-to-exe.md`](p3/P-020-data-dir-next-to-exe.md) |
| **P-021** | `TRAP-P3-004` | PATH 里的旧 `cmake.exe` 遮蔽基线 CMake，生成器不可用 | [`P-021-stale-cmake-shadowing-path.md`](p3/P-021-stale-cmake-shadowing-path.md) |
| **P-022** | `TRAP-P3-005` | 部署目录缺 `qoffscreen.dll`，offscreen 冒烟会「假存活」 | [`P-022-deploy-missing-qoffscreen.md`](p3/P-022-deploy-missing-qoffscreen.md) |

### P4 · traps · P4 — 内容层（真实踩坑记录）（6 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-023** | `TRAP-P4-001` | 构建目录缺 `qoffscreend.dll`，`test_smoke` 异常退出 | [`P-023-debug-build-missing-qoffscreend.md`](p4/P-023-debug-build-missing-qoffscreend.md) |
| **P-024** | `TRAP-P4-002` | `slots` 与 Qt 关键字宏同名，`QObject` 相关头全部编译失败 | [`P-024-slots-qt-keyword-macro.md`](p4/P-024-slots-qt-keyword-macro.md) |
| **P-025** | `TRAP-P4-003` | `markClaimed` 只判 `done`，SQLite 重复领取仍返回成功 | [`P-025-markclaimed-duplicate-claim.md`](p4/P-025-markclaimed-duplicate-claim.md) |
| **P-026** | `TRAP-P4-004` | 成长日记时间戳以 1970 起算（elapsed 时钟泄漏到内容层） | [`P-026-diary-timestamp-epoch-leak.md`](p4/P-026-diary-timestamp-epoch-leak.md) |
| **P-027** | `TRAP-P4-005` | 「状态 / 日常 / 设置」三处签到状态不同步 | [`P-027-signin-state-three-views-desync.md`](p4/P-027-signin-state-three-views-desync.md) |
| **P-028** | `TRAP-P4-006` | 每日固定任务「今日签到」永不完成（签到从未上报 `Interaction::Signin`） | [`P-028-daily-signin-quest-never-done.md`](p4/P-028-daily-signin-quest-never-done.md) |

### P5 · traps · P5 — 梗聊天（真实踩坑记录）（3 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-029** | `TRAP-P5-001` | 关键词立绘不能靠 `"meme-" + id` 拼接 | [`P-029-meme-pose-id-concat.md`](p5/P-029-meme-pose-id-concat.md) |
| **P-030** | `TRAP-P5-002` | 语料分文件后，一致性检查只加载单文件导致漏检 | [`P-030-corpus-split-check-single-file.md`](p5/P-030-corpus-split-check-single-file.md) |
| **P-031** | `TRAP-P5-003` | `CHAT.md` 写「13 种梗」，源文件实为 21 项 | [`P-031-chat-doc-count-mismatch.md`](p5/P-031-chat-doc-count-mismatch.md) |

### P6 · traps · P6 — 设置 / 打磨 / 测试 / 打包（真实踩坑记录）（7 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-032** | `TRAP-P6-001` | `qs()` 缺 `const char*` 重载，`nullptr` 经隐式转换进 `strlen` 崩溃 | [`P-032-qs-null-const-char-overload.md`](p6/P-032-qs-null-const-char-overload.md) |
| **P-033** | `TRAP-P6-002` | `upsert` 先删后插，重绑关键词会重排热词优先级 | [`P-033-hotword-upsert-priority-reorder.md`](p6/P-033-hotword-upsert-priority-reorder.md) |
| **P-034** | `TRAP-P6-003` | `test_growth` 全量运行时一次性失败、无断言输出，随后不可复现 | [`P-034-test-growth-flaky-once.md`](p6/P-034-test-growth-flaky-once.md) |
| **P-035** | `TRAP-P6-004` | PowerShell 5.1 按 ANSI 解析无 BOM 的 UTF-8 脚本，中文乱码致语法错误 | [`P-035-ps51-ansi-utf8-no-bom.md`](p6/P-035-ps51-ansi-utf8-no-bom.md) |
| **P-036** | `TRAP-P6-005` | 找小猫三连 Bug：方向键被下拉框吃掉 / 地图被 `setFixedSize(0,0)` 锁死 / 起点标记不消失 | [`P-036-kitten-multibug-nav-combo.md`](p6/P-036-kitten-multibug-nav-combo.md) |
| **P-037** | `TRAP-P6-006` | 找小猫「隐形墙」：场景切换后未按新场景重建网格，行列错位致显示与判定不符 | [`P-037-kitten-ghost-wall-grid-rebuild.md`](p6/P-037-kitten-ghost-wall-grid-rebuild.md) |
| **P-038** | `TRAP-P6-007` | NSIS 打包：卸载残留 + 64 位安装视图不一致 + 安装目录无写权限致拖拽投喂不可用 | [`P-038-nsis-uninstall-view-writability.md`](p6/P-038-nsis-uninstall-view-writability.md) |

### P7 · 踩坑记录 · P7（插件化智能桌宠与本地 Context API）（20 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-039** | `TRAP-P7-001` | `emit` 不能用作成员函数名 | [`P-039-emit-member-function-name.md`](p7/P-039-emit-member-function-name.md) |
| **P-040** | `TRAP-P7-002` | `QStringLiteral` 只接受字面量 | [`P-040-qstringliteral-literal-only.md`](p7/P-040-qstringliteral-literal-only.md) |
| **P-041** | `TRAP-P7-003` | lambda 不能带默认参数 → 错误回调改用显式类型别名 | [`P-041-lambda-default-argument.md`](p7/P-041-lambda-default-argument.md) |
| **P-042** | `TRAP-P7-004` | moc 无法解析断言宏内的花括号初始化列表 | [`P-042-moc-braced-init-in-assert-macro.md`](p7/P-042-moc-braced-init-in-assert-macro.md) |
| **P-043** | `TRAP-P7-005` | 同步能力失败被误判为「异步已受理」（契约缺陷） | [`P-043-sync-capability-failure-contract.md`](p7/P-043-sync-capability-failure-contract.md) |
| **P-044** | `TRAP-P7-006` | 锁屏被判成 `unknown` 而不是 `afk`（判定顺序缺陷） | [`P-044-lock-state-unknown-order.md`](p7/P-044-lock-state-unknown-order.md) |
| **P-045** | `TRAP-P7-007` | 默认装配误关低层钩子，生产路径静默退化为差分降级 | [`P-045-default-wiring-hook-off.md`](p7/P-045-default-wiring-hook-off.md) |
| **P-046** | `TRAP-P7-008` | 测试桩 MCP server 用 `QFile(FILE*)` 读 stdin，子进程在管道下完全不可用 | [`P-046-mcp-stub-qfile-stdin.md`](p7/P-046-mcp-stub-qfile-stdin.md) |
| **P-047** | `TRAP-P7-009` | `signals` 是 Qt 关键字宏，用作变量名导致「语法错误: public」 | [`P-047-signals-qt-keyword-macro.md`](p7/P-047-signals-qt-keyword-macro.md) |
| **P-048** | `TRAP-P7-010` | 桥接把裸 JSON 写入命名管道，对端 `StdioTransport` 只认分帧 → 无响应 | [`P-048-bridge-raw-json-vs-framing.md`](p7/P-048-bridge-raw-json-vs-framing.md) |
| **P-049** | `TRAP-P7-011` | `std::fread` 读 stdin / 管道会阻塞到读满请求字节数 → 永久死锁 | [`P-049-fread-pipe-blocks-deadlock.md`](p7/P-049-fread-pipe-blocks-deadlock.md) |
| **P-050** | `TRAP-P7-012` | 单测 `connectToServer()` 后同步等信号 → 每个用例白等 5s | [`P-050-test-connect-server-blank-wait.md`](p7/P-050-test-connect-server-blank-wait.md) |
| **P-051** | `TRAP-P7-013` | 本地 Context API 的 HTTP 通道可被浏览器跨站调用 | [`P-051-http-crosssite-origin-fail-open.md`](p7/P-051-http-crosssite-origin-fail-open.md) |
| **P-052** | `TRAP-P7-014` | `windows.h` 的 `max` 宏污染 `std::numeric_limits<T>::max()` | [`P-052-windows-h-max-macro.md`](p7/P-052-windows-h-max-macro.md) |
| **P-053** | `TRAP-P7-015` | JSON 数字是 `double` ⇒ 小数静默截断、超范围值触发未定义行为 | [`P-053-json-number-double-truncation.md`](p7/P-053-json-number-double-truncation.md) |
| **P-054** | `TRAP-P7-016` | 桥接 socket 的「单次未超时就继续」循环可被永久阻塞 | [`P-054-bridge-socket-slow-loop.md`](p7/P-054-bridge-socket-slow-loop.md) |
| **P-055** | `TRAP-P7-017` | 裸 `QTcpServer` 冒充 CDP 端点 ⇒ 握手就超时，测不到真正的路径 | [`P-055-fake-cdp-server-no-handshake.md`](p7/P-055-fake-cdp-server-no-handshake.md) |
| **P-056** | `TRAP-P7-018` | 假 TCP 服务与被测代码同线程 ⇒ 被测的阻塞读饿死服务端 | [`P-056-fake-server-same-thread-starvation.md`](p7/P-056-fake-server-same-thread-starvation.md) |
| **P-057** | `TRAP-P7-019` | 指针链地址加法无溢出检查 ⇒ 野指针 + 偏移绕回，读到无关内存 | [`P-057-pointer-chain-address-overflow.md`](p7/P-057-pointer-chain-address-overflow.md) |
| **P-058** | `TRAP-P7-020` | `static_cast<long long>` 转换 NaN/±Inf 是未定义行为 | [`P-058-nan-inf-to-int-ub.md`](p7/P-058-nan-inf-to-int-ub.md) |

### P8 · traps · P8 — 时段常驻立绘 / 工作立绘池 / 预设对话（真实踩坑记录）（10 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-059** | `TRAP-P8-001` | `kCodingPose` 定义被误删 → C2065 | [`P-059-kcodingpose-deleted-c2065.md`](p8/P-059-kcodingpose-deleted-c2065.md) |
| **P-060** | `TRAP-P8-002` | `PresetDialogueTable::clear()` 只声明未定义 → LNK2019 | [`P-060-presetdialogue-clear-lnk2019.md`](p8/P-060-presetdialogue-clear-lnk2019.md) |
| **P-061** | `TRAP-P8-003` | `SettingsDialog.cpp` 缺 `#include <QLineEdit>` → C2027 | [`P-061-settingsdialog-missing-qlineedit.md`](p8/P-061-settingsdialog-missing-qlineedit.md) |
| **P-062** | `TRAP-P8-004` | 编程时立绘闪一下 `work-ram`（播报与常驻立绘不同源） | [`P-062-work-pose-flash-ram.md`](p8/P-062-work-pose-flash-ram.md) |
| **P-063** | `TRAP-P8-005` | 档位扩容后 `core + warm ≥ kCacheCapacity` → 预载逐出 core | [`P-063-cache-capacity-evicts-core.md`](p8/P-063-cache-capacity-evicts-core.md) |
| **P-064** | `TRAP-P8-006` | `test_context_http_security` 并行运行时偶发失败 | [`P-064-http-security-test-parallel-flaky.md`](p8/P-064-http-security-test-parallel-flaky.md) |
| **P-065** | `TRAP-P8-007` | 成员函数取名 `slots()` 被 Qt 关键字宏展开 → C2059 | [`P-065-slots-member-c2059.md`](p8/P-065-slots-member-c2059.md) |
| **P-066** | `TRAP-P8-008` | 满值常驻位于时段态之前 → 深夜 / 傍晚立绘永不显示 | [`P-066-full-meter-pose-order-override.md`](p8/P-066-full-meter-pose-order-override.md) |
| **P-067** | `TRAP-P8-009` | 沙箱时钟落后于构建产物 → MSBuild 增量构建**静默跳过重编译** | [`P-067-sandbox-clock-msbuild-skip.md`](p8/P-067-sandbox-clock-msbuild-skip.md) |
| **P-068** | `TRAP-P8-010` | 深夜「唤醒窗口」跨时段泄漏 + 跨时段立绘延迟 | [`P-068-night-wake-window-leak.md`](p8/P-068-night-wake-window-leak.md) |

### EXT0 · traps · extend0 — 扩展功能踩坑记录（打包分发后的拖拽投喂可用性）（5 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-069** | `TRAP-EXT0-001` | 安装版拖拽显示「禁止投放」，根源是安装器以提权身份拉起程序（UIPI 跨完整性级别拦截） | [`P-069-nsis-uipi-dragdrop-elevation.md`](ext0/P-069-nsis-uipi-dragdrop-elevation.md) |
| **P-070** | `TRAP-EXT0-002` | 分辨率调整 / 显示器增删后立绘「看不见」：启动沿用历史坐标，未按当前屏幕重算 | [`P-070-display-change-offscreen-position.md`](ext0/P-070-display-change-offscreen-position.md) |
| **P-071** | `TRAP-EXT0-003` | 新增贴边立绘只更新了清单的一半：`home-bottom` 未入 `assets.qrc`，且 `kPoseCount` 与清单条数不一致 | [`P-071-pose-manifest-qrc-count-mismatch.md`](ext0/P-071-pose-manifest-qrc-count-mismatch.md) |
| **P-072** | `TRAP-EXT0-005` | QTest 用例失败时 CTest 拿不到任何输出（`-o -,txt` 静默失效） | [`P-072-ctest-no-output-on-failure.md`](ext0/P-072-ctest-no-output-on-failure.md) |
| **P-073** | `TRAP-EXT0-006` | `PoseLibrary` 自身不初始化 qrc，静默依赖 `main()` 的调用顺序 | [`P-073-poselibrary-qrc-init-order.md`](ext0/P-073-poselibrary-qrc-init-order.md) |

### EX1 · 踩坑记录 · EX1（游戏内存陪玩：Cheat Engine 分析 + 运行期只读感知）（12 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-074** | `TRAP-EX1-001` | CMake 生成失败——声明与列名不一致导致源文件缺失 | [`P-074-cmake-source-list-mismatch.md`](ex1/P-074-cmake-source-list-mismatch.md) |
| **P-075** | `TRAP-EX1-002` | `resolve()` 为 const 却调用非 const 辅助方法（MSVC C2662） | [`P-075-resolve-const-c2662.md`](ex1/P-075-resolve-const-c2662.md) |
| **P-076** | `TRAP-EX1-003` | `QRegularExpression` 分段原始字符串——`(?:` 首字符被当作分隔符吞掉 | [`P-076-qregexp-raw-literal-delimiter.md`](ex1/P-076-qregexp-raw-literal-delimiter.md) |
| **P-077** | `TRAP-EX1-004` | 运行期调用 `mono.dll` 导出函数 == 在目标进程执行代码（红线决策） | [`P-077-mono-dll-inprocess-call-redline.md`](ex1/P-077-mono-dll-inprocess-call-redline.md) |
| **P-078** | `TRAP-EX1-005` | MV/MZ 的 CDP 通道并非总可用（双通道路由决策） | [`P-078-rpgmaker-cdp-availability.md`](ex1/P-078-rpgmaker-cdp-availability.md) |
| **P-079** | `TRAP-EX1-006` | `QWebSocket` 销毁期的 Qt 内部告警（噪声，勿误判为缺陷） | [`P-079-qwebsocket-destroy-warning-noise.md`](ex1/P-079-qwebsocket-destroy-warning-noise.md) |
| **P-080** | `TRAP-EX1-007` | 未过期一次性姿态期间到达的里程碑/陪玩态被让位（符合设计，非缺陷） | [`P-080-oneshot-pose-preemption-by-design.md`](ex1/P-080-oneshot-pose-preemption-by-design.md) |
| **P-087** | `TRAP-EX1-008` | RPG Maker MV 发行版自带 NW.js 为非 SDK 构建，`--remote-debugging-port` 不监听（CDP 通道不可用） | [`P-087-mv-nwjs-non-sdk-no-cdp.md`](ex1/P-087-mv-nwjs-non-sdk-no-cdp.md) |
| **P-088** | `TRAP-EX1-009` | Cpp2IL 的 `diffable-cs` 偏移注释格式与 `UnityDumpConverter` 不兼容（SOP 误称其产出 `dump.cs`） | [`P-088-cpp2il-offset-format-mismatch.md`](ex1/P-088-cpp2il-offset-format-mismatch.md) |
| **P-089** | `TRAP-EX1-010` | Context API 随设置持久化启用时 HTTP 通道不监听（`start()` 非幂等 + 原子回滚） | [`P-089-context-api-http-not-listening-on-restore.md`](ex1/P-089-context-api-http-not-listening-on-restore.md) |
| **P-090** | `TRAP-EX1-011` | 桥接快照无新鲜度校验：游戏退出后桌宠持续展示陈旧数据 | [`P-090-bridge-snapshot-no-freshness.md`](ex1/P-090-bridge-snapshot-no-freshness.md) |
| **P-091** | `TRAP-EX1-012` | MV「全屏图片」判据对角色立绘 / 图片化 UI 误判，`specialScene` 误报触发静默陪伴 | [`P-091-mv-fullscreen-picture-false-positive.md`](ex1/P-091-mv-fullscreen-picture-false-positive.md) |

### P9 · 踩坑记录 · P9（渐进式插件化：P9-A 宿主服务注册化 + P9-B 外部进程型深化 + P9-C UI 宿主契约与贡献点，✅ 全部验收通过）（4 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-081** | — | 能力模板把「服务指针」传给期望「插件指针」的快照回调 → C2440/C2664 | [`P-081-service-capability-snapshot-signature.md`](p9/P-081-service-capability-snapshot-signature.md) |
| **P-082** | — | 新增插件漏 include 宿主类型头 → C2027 / C2039 | [`P-082-missing-include-host-type.md`](p9/P-082-missing-include-host-type.md) |
| **P-083** | — | 断言宏内含逗号的花括号初始化列表被当作多参数 → C2187/C2958（重复命中 `P-042`） | [`P-083-qcompare-braced-init-comma.md`](p9/P-083-qcompare-braced-init-comma.md) |
| **P-084** | — | 测试断言把替身工厂的固定字段当成插件 id（误判被测实现） | [`P-084-test-double-field-assert-mismatch.md`](p9/P-084-test-double-field-assert-mismatch.md) |

### EX2 · 踩坑记录 · EX2（发布打包脚本修复：PowerShell 退出码 + makensis 参数）（2 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-085** | — | 发布脚本误报工具失败（PS 5.1 `Start-Process -PassThru` 的 `ExitCode` 为 `$null`） | [`P-085-powershell-start-process-exitcode-null.md`](ex2/P-085-powershell-start-process-exitcode-null.md) |
| **P-086** | — | makensis 参数非法（`/INPUTCHARSET` 的值必须为独立 token） | [`P-086-makensis-inputcharset-single-token.md`](ex2/P-086-makensis-inputcharset-single-token.md) |

### EX3 · 踩坑记录 · EX3（移除外部游戏陪玩：gamestate 归档 + 数据源抽象泛化）（2 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-092** | — | 移除静态库目标后，复用旧构建目录出现 MSB8064 增量依赖告警 | [`P-092-stale-msbuild-autogen-deps-after-target-removal.md`](ex3/P-092-stale-msbuild-autogen-deps-after-target-removal.md) |
| **P-093** | — | 移除服务成员后遗漏调用方，编译期 C2039 | [`P-093-removed-member-still-referenced-c2039.md`](ex3/P-093-removed-member-still-referenced-c2039.md) |

### EX4 · 踩坑记录 · EX4（小游戏陪玩：插件侧自描述状态 + 陪玩侧通用聚合；发布脚本回归；接 Token 验证轮）（7 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-094** | — | 新增 `.cpp` 漏 include 声明所属头，编译期 C2039 / C3861 | [`P-094-new-cpp-missing-own-header-c2039.md`](ex4/P-094-new-cpp-missing-own-header-c2039.md) |
| **P-095** | — | `T x(U())` 被解析成函数声明（most vexing parse）→ C2228 | [`P-095-most-vexing-parse-c2228.md`](ex4/P-095-most-vexing-parse-c2228.md) |
| **P-096** | — | PowerShell 管道吞掉构建退出码，编译失败被误判为成功（**重复命中 P-004**） | [`P-096-powershell-pipeline-swallows-exit-code-recur.md`](ex4/P-096-powershell-pipeline-swallows-exit-code-recur.md) |
| **P-097** | — | 发布产物随包不含 `qoffscreen.dll`，发布级冒烟不能用 offscreen | [`P-097-release-portable-no-qoffscreen-smoke.md`](ex4/P-097-release-portable-no-qoffscreen-smoke.md) |
| **P-098** | — | 安装包需提权（UAC），静默安装/卸载往返无法在自动化会话完成 | [`P-098-installer-requires-elevation-silent-roundtrip.md`](ex4/P-098-installer-requires-elevation-silent-roundtrip.md) |
| **P-099** | — | 测试把 UTF-8 中文文案按 `fromLatin1` 解码，断言乱码失败 | [`P-099-test-utf8-literal-fromlatin1.md`](ex4/P-099-test-utf8-literal-fromlatin1.md) |
| **P-100** | — | 把「越界点击必然产生位移」当作断言，与 `move*` 返回「是否发生变化」的契约冲突 | [`P-100-move-return-means-changed-not-out-of-range.md`](ex4/P-100-move-return-means-changed-not-out-of-range.md) |

### EX5 · 踩坑记录 · EX5（小游戏体验优化：国际象棋难度梯度与棋盘朝向 / 找小猫地图翻倍与物品文案）（3 条）

| 序号 | 原编号 | 标题 | 文件 |
|---|---|---|---|
| **P-101** | — | PowerShell 5.1 `Set-Content -Encoding UTF8` 重写源码引入 BOM/CRLF，编辑工具随即无法匹配 | [`P-101-powershell-setcontent-bom-crlf.md`](ex5/P-101-powershell-setcontent-bom-crlf.md) |
| **P-102** | — | Python 一行式「就地读写」先截断后读取 → 4 个源文件被清空（靠 `git show HEAD:<path>` 逐字节恢复） | [`P-102-python-inplace-read-write-truncates.md`](ex5/P-102-python-inplace-read-write-truncates.md) |
| **P-103** | — | 界面回归用例以「跨难度的格数」为基准，地图尺寸一变即误报；顺带修正守卫未真正咬到下拉框路径的强度问题 | [`P-103-cross-difficulty-cellcount-baseline.md`](ex5/P-103-cross-difficulty-cellcount-baseline.md) |

## 二、非条目归档（新增条目模板 · 阶段实测结论 · 待人工验收项）

> 迁移自原 `docs/traps-*.md`，非踩坑条目，仅供备查。

## P1 实测结论（供 `ROADMAP-P1.md` 验收参考）

> 摘自 `docs/pitfalls/`（迁移前文件已删除）。

| 验收项 | 结果 | 说明 |
|---|---|---|
| Debug 可构建 | ✅ | `cmake --build build --config Debug --parallel` |
| Release 可构建 | ✅ | `cmake --build build --config Release --parallel` |
| CTest 通过 | ✅ | `test_smoke` Passed（offscreen，0.18s） |
| 部署后独立启动 | ✅ | `deploy-release/` 补齐 exe 后可运行，`qwebp.dll` 已随部署 |
| 空闲内存 < 30MB | ✅ | 稳态 WorkingSet ≈ 29.27MB |
| 空闲 CPU < 5% | ✅ | 实测 ≈ 0% |
| 立绘显示 / 无方框 / 置顶 / 拖拽 / 右键菜单 / 托盘 / 位置持久化 | ⏳ 待人工交互确认 | 已实现且进程不崩溃；透明置顶、拖拽、菜单交互需在真实桌面目视确认 |

> 结论：P1 的**工程骨架与外壳代码已跑通构建 / 测试 / 部署 / 启动**；
> 因「透明无方框、置顶不抢焦点、拖拽与右键菜单交互」需人工在桌面核验，
> 故 `ROADMAP-P1.md` **暂不追加 `Fin`**，待目视验收通过后再改名。

---

## P2 实测结论（截至本次记录）

> 摘自 `docs/pitfalls/`（迁移前文件已删除）。

| 验收项 | 结果 | 说明 |
|---|---|---|
| Debug 可构建 | ✅ | `cmake --build build --config Debug --parallel` |
| Release 可构建 | ✅ | `cmake --build build --config Release --parallel` |
| `test_state_machine` | ✅ | 上下文态 / 交互 / 时序 / 概率 / 关键词 / 清单完整性 |
| `test_line_table` | ✅ | 解析、取用、最近去重、与状态机输出场景一致性 |
| `test_smoke` | ✅ | offscreen 下创建窗口并切换姿态；特效序号去重 + 500ms 间隔（含「丢弃不补播」）；台词序号去重 + 流式打断 |
| CTest 汇总 | ✅ | Debug 与 Release 均 **3/3 Passed**（需先注入 Qt `bin`，见 `TRAP-P2-008`）；`test_smoke` 共 6 个用例通过 |
| 92 张立绘入资源 | ✅ | 统一 256×256，清单由脚本生成（见 `TRAP-P2-005`） |
| 程序化动效（呼吸/摇摆/惯性/点击反馈/过渡遮断/粒子） | ✅ **人工目视通过** | 真实桌面验收：姿态切换无叠影/闪黑，拖拽摇摆与惯用手感正常 |
| 空闲占用 | ✅ **人工实测通过** | 空闲 **0.0%–0.1%** CPU（判据 < 5%）；拖动 **0.3%–0.4%** CPU |
| 偶发 `0xC0000409` | ⚠️ **未定位（长期观察项）** | 见 `TRAP-P2-007`：AI 侧 offscreen 自动化场景偶发，用户侧多轮人工复验均**未观测到**，暂缓观察 |

> 结论：P2 的**功能实现、自动化验证与人工目视验收均已通过**（人工复验 9 项：8 通过 + 1 长期观察，2026-09-30），
> 故 `ROADMAP-P2.md` → `ROADMAP-P2-Fin.md` 完成改签。
> 唯一遗留 `TRAP-P2-007` 为**未定位的长期观察项**，按 `README.md` §六 不阻塞收尾。

---

## 附：验证顺带修掉的「与运行时刻耦合」的测试

> 摘自 `docs/pitfalls/`（迁移前文件已删除）。

非产品缺陷，但会导致**偶发红**，已一并加固：

1. `tests/test_growth.cpp::signInIsIdempotentPerDay`
   原用 `baseMs()`（当前时刻）作基准，断言 `signIn(base + 3600000)` 仍属同一天；
   若恰在午夜前 1 小时内跑，`+1h` 就跨了自然日 → 随机失败。
   新增 `noonMs()`（当天 12:00）作基准，与运行时刻解耦。
2. `tests/test_content.cpp::calendarWeekAndNightWindow`
   原断言 `weekKey(now) == weekKey(now + 7天)`，**把「一周后」当成了「同一周」**；
   正确关系是 `dayIndex` 相同、`weekKey` 前进一周。已改为断言二者不等，
   并各自校验 `weekKey(now) == dayKeyOffset(now, -dayIndexMondayFirst(now))`。
3. （后续轮次发现）`tests/test_growth.cpp::satietyDecayUsesIntegerPoints` 与
   `companionTimeAccumulates`：
   `GrowthService::load()` 以「载入当时墙钟」为结算基准 `m_lastSettleMs`，而测试的
   `base = baseMs()` 在其**之后**才取，两者相差 `d` 毫秒（通常 0，偶发 ≥1）。
   - `settle(base + kMsPerSatietyPoint - 1)` 的实际 elapsed 变成 `k-1+d`，
     `d≥1` 时误掉 1 点 → 断言失败（本次复现到的就是它）；
   - `settle(base + kGrowthTickMs)` 的 elapsed 变成 `k+d` → `companionMs` 多出 `d`。
   修法：在 `load()` 后用 `growth.resetToDefaults(base)` 把结算基准与衰减余量**显式锁定**
   到 `base`（不是放宽阈值），断言即完全确定。`ctest -R test_growth --repeat until-fail:60`
   全绿验证。

---

---

## 待登记模板（仅作格式示例，条目必须由真实问题产生后才可写入）

> 摘自 `docs/pitfalls/`（迁移前文件已删除）。

### TRAP-P7-XXX：<一句话现象>

- **现象（可复现步骤 / 报错原文）**：
  1. …
  - 报错原文（逐字）：
    ```
    …
    ```
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，配置 Debug/Release，二进制路径 …
- **根因**：
- **解决或规避**：
- **影响与关联文档**：

---

## 记录模板（新增条目照此格式）

> 摘自 `docs/pitfalls/`（迁移前文件已删除）。

---

## TRAP-EX1-000：一句话现象

> 摘自 `docs/pitfalls/`（迁移前文件已删除）。

- **现象**：可复现步骤 + 报错原文（逐字，含文件:行号）。
- **根因**：最小化到具体机制，说明为什么会这样。
- **解决或规避**：实际采取的修复动作（不是「可能」「也许」）。
- **影响与关联文档**：涉及的文件/符号，关联的路线图章节。

---

---

## 待人工验收项（非踩坑，登记备查）

> 摘自 `docs/pitfalls/`（迁移前文件已删除）。

> 需真实游戏与人工操作，不计入自动化测试；完成后回填结论与日期。

| 编号 | 验收项 | 关联阶段 | 状态 |
|---|---|---|---|
| ACC-P9-001 | P9-A 宿主服务经 builtin 层注册化（A1~A6 逐条通过） | P9-A | ✅ 2026-10-05 验收通过 |
| ACC-P9-002 | P9-B 外部进程型深化（A1~A6 逐条通过） | P9-B | ✅ 2026-10-05 验收通过 |
| ACC-P9-003 | P9-C UI 宿主契约与贡献点协议（C1~C6 逐条通过） | P9-C | ✅ 2026-10-06 验收通过 |
| ACC-EX4-001 | 打开任一小游戏窗口 → 桌宠进入陪玩态；象棋被将军 → `Danger`；通关 → 播报一次 `game.clear`；关闭窗口 → 陪玩采样自动停（无残留）。**需真实桌面目视** | EX4 | 待人工验收 |
| ACC-EX4-002 | 0.3.0 安装包：静默安装到临时目录 → 静默卸载 → 目录清空（逐条核对 `packages.md` §四对应表）。**需提权 UAC，自动化无法完成** | EX4 | 待人工验收 |
| ACC-EX4-003 | 小游戏「接Token」目视验收：手感（← / → 与点击某列移接取区）、**接到白饭时 `daily-picnic` 立绘的显示时机与停留**、连击 / 提速台词节奏、白饭与 Token（「饭」/「币」）的可辨识度、窗口隐藏再打开后的暂停与恢复。**需真实桌面目视** | EX4 追加 | 待人工验收 |
| ACC-EX1-001 | ≥1 个 Unity **Mono** 单机游戏端到端读出约定字段 | EX1.2 | 待验收 |
| ACC-EX1-002 | ≥1 个 Unity **IL2CPP** 单机游戏端到端读出约定字段 | EX1.2 | ⛔ 2026-10-07 受阻：目标 `The Piper Of Dawn` 经 Cpp2IL 反编译成功，但其 `diffable-cs` 偏移格式与 `UnityDumpConverter` 不兼容（见 P-088），且程序集经 `OPS.Obfuscator` 混淆、无 `hp/gold` 语义字段，离线无法确定约定字段 |
| ACC-EX1-003 | ≥1 个 RPG Maker **MV/MZ** 单机游戏经 CDP 只读读出金币/变量/坐标 | EX1.3 | ✅ 2026-10-07 **达成（经方案 B 只读桥接；由 owner 裁定计入本项）**：CDP 因该发行版为 NW.js **非 SDK** 构建而不可用（P-087，CDP 受阻仍保留记录）；改由只读桥接插件（`docs/samples/rpgmaker-mv/WhalePetBridge.js`）读出真实 `hp/hpMax/level/gold/posX/posY/mapName` 并驱动桌宠（`context.gameState` 取证：`available=true`、`engine=rpgmaker-mv`、`mood=normal`、`confidence=0.9`） |
| ACC-EX1-004 | 特殊场景 CG（图片 / 专用场景 / 影片至少各 1 例）识别并驱动静默陪伴 | EX1.3 | 🔶 2026-10-07 部分达成 + 误判已修：目标游戏把**角色立绘**以 922×922 / opacity 255 / cover 1.081 绘制而被误判为 `Picture` 的问题，已用**可配置图片名排除名单**修复（P-091，真机 `specialScene` 由 1 → 0、`silent` 由 true → false）；影片 / 专用场景 / 对话演出三类判据仍待逐一取得真机样例 |
| ACC-EX1-005 | ≥1 个 RPG Maker **RGSS（XP/VX/VX Ace）** 单机游戏经只读脚本桥接读出字段 | EX1.3 | 待验收 |
| ACC-EX1-006 | 开启「游戏陪玩」后端到端触发立绘/台词（升级/BOSS/通关各 ≥1 例），且进入特殊场景（CG/影片/专用场景/对话）时静默陪伴 | EX1.4 | 🔶 2026-10-07 部分达成：数据链路端到端可用（`available=true` / `engine=rpgmaker-mv` / `mood=normal` / `confidence=0.9`）；静默陪伴**误触发已修**（P-091）；升级 / BOSS / 通关里程碑仍未逐一真机触发 |
| ACC-EX1-007 | 关闭「游戏陪玩」后运行行为与 EX1 前一致（无进程打开、无 game.* 台词、进程退出后自动回到正常陪伴态） | EX1.4 | ✅ 2026-10-07 通过（本机可验范围）：快照文件缺失如实降级（不伪造）；**游戏进程退出后经 `ts` 新鲜度校验如实降级** `available=false / mood=unknown`，游戏重启后自动恢复（P-090 已修）；「关闭时零开销 / 不打开进程」由离线测试覆盖 |

---

