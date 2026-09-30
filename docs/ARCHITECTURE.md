# 总体架构设计（ARCHITECTURE）

## 1. 目标与范围

构建一个 **Windows 独立桌宠应用**：单进程、原生 Qt Widgets、无宿主依赖。核心能力 = **桌宠外壳**（透明置顶窗口 / 拖拽 / 菜单 / 立绘显示）+ **养成系统** + **梗聊天**。

## 2. 技术选型

| 项 | 选择 | 说明 |
|---|---|---|
| 语言 | C++17 | 固定标准 |
| UI | Qt 6.8.4 **Widgets** | 原生窗口，非 QML、非 WebEngine |
| 图像 | Qt6::Gui（含 webp 图像插件） | 立绘为 webp，构建期确认 `imageformats/qwebp` |
| 数据 | **Qt6::Sql（QSQLITE）** | SQLite 为 Qt 内建模块，**不引入第三方库** |
| 测试 | Qt6::Test | 自研用例 |
| 构建 | CMake 4.4.2 + VS 18 2026 | 详见 `BUILD.md` |

> 约束：**零新依赖**。SQLite 走 `Qt6::Sql` 的 QSQLITE 驱动，无需外部库。

## 3. 分层结构

```
┌────────────────────────── View（Qt Widgets） ──────────────────────────┐
│ PetWindow      透明/无边框/置顶主窗口，承载立绘                          │
│ PoseView       立绘渲染 + 程序化动效（缩放/位移/旋转/透明度）             │
│ SpeechBubble   台词气泡                                                  │
│ TrayMenu       右键菜单 / 系统托盘                                       │
│ SettingsDialog 设置面板                                                  │
│ Panels         状态/成长/成就/任务/日记等展示面板                        │
└─────────────────────────────────────────────────────────────────────────┘
                 ▲ 数据绑定(只读属性) / 命令          │ 用户事件
                 │                                    ▼
┌──────────────────── ViewModel / Controller ─────────────────────────────┐
│ PetController     总调度：连接事件 → 状态机 → 表现/数据                  │
│ PosePresenter     pose/动效/气泡指令的发布者                             │
│ GrowthService     养成数值计算（心情/好感/饱食/等级/签到/陪伴时长）       │
│ AchievementService 成就判定                                             │
│ QuestService      每日任务 / 周签到                                      │
│ ChatService       台词选择 + 关键词表情感知                              │
└─────────────────────────────────────────────────────────────────────────┘
                 ▲ 纯逻辑调用                        │
                 ▼                                    ▼
┌──────────────── Core（无 UI 依赖，可单测） ────────┬──── Model（数据） ───┐
│ PetStateMachine  状态机（移植 whale core.js）      │ Database   SQLite 封装│
│ Rules            数值/成就/任务纯规则函数          │ Repositories 各表访问 │
│ LineTable        台词库解析/匹配                   │ Schema    建表/迁移   │
└───────────────────────────────────────────────────┴───────────────────────┘
```

**设计原则**
- **Core 层零 Qt UI 依赖**：状态机与规则为纯 C++，可脱离界面单测（延续 whale `core.js` 的优点）。
- **单向数据流**：View 只发事件、只读属性；所有写入经 Controller → Service → Model。
- **表现与逻辑分离**：状态机输出「语义结果」（pose 名 / 台词 key / 特效类型），由 Presenter 翻译成 Qt 动画。

## 4. 模块清单

| 模块 | 职责 | 关键来源 |
|---|---|---|
| `PetWindow` | 透明无边框置顶窗口、拖拽、多显示器、位置持久化 | DesktopPet 思路 |
| `PoseView` | 加载 webp、按 pose 切换、程序化动效 | 新增（替代 QMovie/GIF） |
| `PetStateMachine` | 状态集合与转移 | whale `core.js` |
| `GrowthService` | 心情/好感/饱食/等级/连续签到/陪伴时长 | whale 养成 |
| `AchievementService` | 成就判定与解锁 | whale 成就（39） |
| `QuestService` | 每日任务、周签到 | whale |
| `ChatService` | 台词选取、关键词表情 | whale 台词库（530+） |
| `Database` | SQLite 连接、建表、迁移、降级 | 新增 |
| `SettingsDialog` | 设置项读写 | whale（去宿主化重做） |

> **P3 落地情况**：`model/` 已按上表落地为静态库 `whalepet_model`
> （`Qt6::Core` + `Qt6::Sql`，**不链接 Widgets**，依赖 `whalepet_core`）；
> `GrowthService` 落在 `src/viewmodel/`，其数值规则拆到零 Qt 的 `src/core/GrowthRules.h`
> 以便脱 UI 单测。类清单见 `DATA-MODEL.md` §4 的实施落位表。

## 5. 建议目录结构

```
desktoppet/
├─ src/
│  ├─ app/          # main.cpp、应用装配、生命周期
│  ├─ view/         # PetWindow / PoseView / SpeechBubble / TrayMenu / SettingsDialog / Panels
│  ├─ viewmodel/    # PetController / *Service / Presenter
│  ├─ core/         # PetStateMachine / Rules / LineTable（无 UI 依赖）
│  ├─ model/        # Database / Repositories / Schema
│  └─ common/       # 常量、工具、事件定义、类型
├─ assets/
│  ├─ poses/        # 92 张 webp 立绘
│  ├─ lines/        # 台词库、关键词表
│  └─ *.qrc
├─ tests/           # 自研单测
├─ docs/            # 本设计文档目录
├─ CMakeLists.txt
└─ CMakePresets.json
```

## 6. 事件与线程模型

- 主线程（Qt GUI 线程）负责全部 UI 与状态机 tick；SQLite 读写默认同步（个人使用量级足够）。
- 定时驱动：**待机 tick**（状态机概率事件）、**AFK 计时**（180s）、**气泡间隔**（≥6s）、**养成结算 tick**（如饱食随时间下降）。
- 若后期 DB 写放大，再引入 `QThread`/`QtConcurrent` 异步落盘（本期不做）。

## 7. 参考项目模块映射

| 新模块 | 移植来源 | 方式 |
|---|---|---|
| `PetWindow` | `DesktopPet/src/view/PetMainWindow.*` | 参考实现，在 MSVC 基线重写（去除 MinGW/QMovie 依赖） |
| `PetStateMachine` | `whale/assets/whale-moe-core.js` | **逻辑移植**为纯 C++ |
| `PoseView` / 动效 | `whale/assets/dsh-whale-moe.js`（presenter） | **重写**为 Qt 动画 |
| 养成/成就/任务 | whale 对应逻辑 | 逻辑移植 + 落 SQLite |
| `ChatService` | whale 台词库 + 关键词表 | 迁移内容 + 移植匹配逻辑 |
| `Database` | `DesktopPet` 的 JSON 持久化 | **替换**为 SQLite |
| 设置面板 | whale 注入式设置 | **重做**为 Qt 对话框 |

## 8. 关键风险与对策

| 风险 | 对策 |
|---|---|
| webp 图像插件缺失 | 构建期探测 `qwebp`，缺失则报错并给出 windeployqt 说明（见 `BUILD.md`） |
| 安装目录写权限（Program Files / UAC） | 默认写「安装目录同级 `data/`」；写失败降级到用户目录并日志告警（见 `DATA-MODEL.md`） |
| 台词语料量大（530+） | 外部资源文件承载，不硬编码进 C++（见 `CHAT.md`） |
| 静态立绘缺动效导致「呆板」 | 用程序化动效补偿（呼吸缩放、惯性位移、过渡遮断，见 `PRESENTATION.md`） |
