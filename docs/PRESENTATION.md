# 表现层与立绘设计（PRESENTATION）

## 1. 立绘资产

- 来源：`referances/dsh-whale-musume/assets/generated/`，共 **92 张 webp** 静态立绘。
- 命名：`dsh-whale-state-<name>.webp`，与本项目**保持同名**，作为 pose 名映射（见 `STATE-MACHINE.md`）。
- 分类概览：

| 类别 | 示例 | 数量级 |
|---|---|---|
| 基础状态 | idle-cute / waiting / thinking / afk / sleep / night / wink | ~10 |
| 情绪 | blush / angry / curious / teasing / bold / abstract | ~8 |
| 结果 | success / failure / celebrate / levelup / achievement / star | ~8 |
| 互动分区 | react-head / react-belly / react-tail / pick-up / tail-swing | ~6 |
| 日常小剧场 | daily-coffee / stretch / eat / gaming / painting / picnic … | ~12 |
| 表情梗 | meme-kyun / omg / doge / sike / worship / peace / doubt … | 13 |
| 节日 | festival-christmas / halloween / mid-autumn / spring / valentine | 5 |
| 其他 peeks | home-peek / settings-peek / workbench-peek | 3 |

## 2. 渲染方案（静态立绘 + 程序化动效）

**不做逐帧动画。** 所有「动」由 Qt 动画在静态 webp 上程序化生成：

| 效果 | 实现手段 | 用途 |
|---|---|---|
| 呼吸/待机微动 | `QPropertyAnimation` 对缩放做周期性微幅变化 | 让待机不呆板 |
| 拖拽摇摆 | 按光标位移方向对旋转角做插值 | 被拎起时自然摆 |
| 拖拽惯性 | 松手后按瞬时速度滑行 + 旋转回正 | 参考 whale 惯性参数 |
| 状态过渡遮断 | 「下压 → 换图 → 弹起」三段动画 | 避免叠影/闪黑 |
| 点击反馈 | 快速缩放回弹 + 位移 | 即时反馈 |
| 特效 | `heart` / `star` / `particle`（自绘 QWidget 或 pixmap 粒子） | 摸头/三连击 |

- 立绘容器用**透明背景**的 `QLabel`/自绘 `QWidget`，按 pose 换 `QPixmap`。
- 立绘**不加滤镜**（沿用 whale 约定），仅做几何变换与透明度。

### 2.1 特效触发规则（P2 增补）

| 规则 | 说明 |
|---|---|
| 一次操作一次迸发 | 由 `PoseResult.fxSerial` 去重（状态机每 tick 重推缓存结果，序号不变 → 不重放） |
| 强制最小间隔 | `kFxMinGapMs` = **500ms**，**所有特效类型共用**；间隔内的新触发**直接丢弃**（不排队、不补播） |
| 同类清理 | 迸发前先清掉同类残留粒子，避免新旧两批叠加被看成「持续迸发」 |
| 不受限 | 点击反馈（下压/上顶的缩放回弹）**不**计入该间隔，保留连点手感 |

> 根因与踩坑：`docs/traps-P2.md` `TRAP-P2-009`。相关常量集中在 `src/common/PetVisuals.h`。

## 3. 窗口行为

| 项 | 设计 |
|---|---|
| 窗口标志 | `Qt::FramelessWindowHint \| Qt::WindowStaysOnTopHint \| Qt::Tool` |
| 背景 | `setAttribute(Qt::WA_TranslucentBackground)` |
| 尺寸 | 默认约 200px（可配置），随立绘等比 |
| 拖拽 | 移动超过 4px 才进入拖拽（避免误触） |
| 位置持久化 | 松手后写入 SQLite（`settings` / `pet_state`） |
| 多显示器 | 记录所在屏幕；越界时夹回可见区域 |
| 退出 | 右键菜单退出 / 系统托盘退出 |
| 找回 | 隐藏后托盘或唤回入口可恢复（避免「找不到」） |
| 菜单层级 | 右键菜单与托盘菜单均置顶（`Qt::WindowStaysOnTopHint` + 显示时 `raise()`），否则会被置顶立绘遮挡 |

## 4. 台词气泡

- 独立无边框子窗口（非模态），跟随立绘位置，流式播完后再停留约 3–5s 消失。
- 样式：深色底、圆角、白字（与工作区全局样式一致；具体外观遵循 qt-ui 规范）。
- 与状态机解耦：只接收 `lineKey` + 文本。

### 4.1 流式输出（P2 增补）

| 项 | 规则 |
|---|---|
| 节奏 | `kStreamCharIntervalMs` = **40ms/字**（≈25 字/秒）；标点后额外停顿 `kStreamPunctPauseMs` = **120ms** |
| 打断 | 新台词到达 → 立即中止当前流式，**清空并从第一个字重新开始**（连点只保留最后一次操作的台词） |
| TTL | 隐藏计时从**流式结束**起算（`总时长 = 打字时长 + ttlMs`），长句不会没打完就消失 |
| 尺寸 | 按**整句**预排版后固定，打字过程中气泡不抖动、位置不跳 |
| 去重 | 由 `PoseResult.lineSerial` 保证「一次操作只播一次」（否则每 tick 都会换一句） |
| 不做 | 不支持「点击跳过打字」——点击一律视为新操作（会触发新台词） |

> 根因与踩坑：`docs/traps-P2.md` `TRAP-P2-009`。

## 5. 交互

| 操作 | 反馈 |
|---|---|
| 单击（分区） | 头/肚/尾 专属立绘 + 特效 + 台词 |
| 三连击 | `star` + 粒子 + 庆祝 |
| 拖拽 | pick-up 立绘 + 惯性滑行 |
| 右键 | 投喂 / 戳一下 / 夸夸 / 回原位 / 设置 / 退出 |
| 双击 | 打开状态面板（可选） |
| 悬停 | `curious`（可选） |

## 6. 性能目标（对齐 DesktopPet 指标）

- 空闲 CPU < 5%，内存 < 30MB，启动 < 0.5s。
- 立绘切换：**首屏只预载常用 5 张**，其余在空闲期惰性加载（`QTimer` 每 ~120ms 取一张），预取失败静默。

## 7. 资源打包

- 立绘/台词随程序分发（`.qrc` 或紧邻数据目录）。
- 个人使用场景优先**外部资源目录**（便于替换立绘、改台词）；打包时随安装目录分发。
