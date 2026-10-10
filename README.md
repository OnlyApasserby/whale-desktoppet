# WhalePet · 鲸鱼娘桌宠

> 基于 **Qt 6 原生底座** 的 Windows 独立桌面宠物：复用「鲸鱼娘」立绘与 whale 的养成 / 梗聊天逻辑，
> **不依赖任何宿主程序**，双击即用。
>
> 当前版本：**0.3.0**

---

## 功能特性

- **桌面常驻**：透明 + 无边框 + 置顶窗口；拖拽移动（松手有惯性滑行）、按高度分区点击反馈（头 / 肚 / 尾）。
- **桌面四边框贴边**：拖到桌面（屏幕可用区域）四条边框 **20px** 以内即**判定贴合**并吸附对齐，
  立绘**立即**换成该方向的**探头立绘**——上 `home-bottom` / 下 `home-peek` / 左 `settings-peek` /
  右 `workbench-peek`；贴边期间**不切拖动立绘**，拖离边框即恢复原姿态
  （判定与贴合规则见 `docs/PRESENTATION.md` §3.1）。
- **养成系统**：等级 / 经验 / 心情 / 好感 / 饱食 / 羁绊 / 累计陪伴 / 连续签到。
- **日常内容**：每日任务、周签到、**39 项成就墙**、成长日记。
- **梗聊天**：分时问候、心情分层台词、羁绊 Lv3/5/7 专属台词；关键词梗表情感知（**默认关**）。
- **自定义热词**：全局热键 `Ctrl+Alt+K` 录入「热词 → 表情」，优先级按录入顺序（详见 `docs/CHAT.md`）。
- **小游戏 · 扫雷**：3 档预设（初级 9×9·10 / 中级 16×16·40 / 高级 30×16·99）+ **自定义尺寸与雷数**，
  界面实时展示当前难度与参数；开局 / 连翻 / 踩雷 / 通关 / 失败会切换鲸鱼娘立绘并播报台词；
  一局结算按「通关 / 及格 / 失败」给养成奖励，**每日 3 局**计入（超出只计分）。
- **小游戏 · 鲸鱼娘找小猫**：Robot Finds Kitten 风格的地图探索——方向键 / WASD / 点击相邻格带鲸鱼娘
  走迷宫，绕过礁石、捡起沿途物件（扇贝 / 海龟 / 海星 / 珍珠 / 螃蟹 / 海螺 / 海草 / 水母 / 漂流瓶 /
  破靴子 / 海胆 / 瓶盖，共 **12 类**），顺着海流切换场景，在最深处找到小猫即通关；
  不同类别物体触发差异化立绘与**专属台词**（物品专属台词 **336 句**，无厘头风格）；
  难度决定需要穿越 1 / 2 / 3 个场景；**单格 UI 15px（相对早期减半）、地图各方向翻倍**
  （面积 ×4，窗口观感尺寸基本不变）。**物体列表、地图与台词均为外部资源**（`assets/maps/`、
  `assets/lines/kitten.txt`），可自行增删替换。
- **小游戏 · 国际象棋**：和鲸鱼娘下一局国际象棋，对手是**外部 UCI 象棋引擎**（如 Stockfish，
  程序**不自带棋力**）——用 `QProcess` 启动引擎并按 **UCI 协议**通信；**点击或拖动**走子
  （点击棋子高亮全部合法落点，点击落点或把棋子拖到落点即落子；落在非法格不移动、棋子回到原格），
  规则（合法着法 / 王车易位 / 吃过路兵 / 兵升变 / 将军将死逼和）由本程序校验，引擎着法同样复核；
  可切换执白 / 执黑与 **六档棋力**（入门 / 休闲 / 中级 / 高级 / 专家 / 大师，
  按**搜索深度 3 / 4 / 6 / 8 / 11 / 14** 与思考时间限强，最低档深度 3）；
  **棋盘默认把你自己的一侧摆在下方**（执黑自动上下对调），并提供「翻转棋盘」手动对调（只上下对调）；
  吃子 / 将军 / 胜负 / 和棋会切换立绘并播报台词。
  **需用户自行准备引擎**，详见下节「国际象棋引擎」。
- **小游戏 · 接Token**：接住从上方落下的 **Token**（界面字「币」）+1 分，别接到 **白饭**（界面字「饭」）——
  **接到白饭本局立即结束，并播放鲸鱼娘野餐立绘 `daily-picnic`**；← / → 或 A / D 移动接取区（也可点击某列），
  达成目标 Token 数（三档 12 / 20 / 30）即通关，时限 60 秒；每接满 5 个 Token 提升一档节奏，
  连击 / 漏接 / 剩余时间实时显示；接入过程同时作为「新增小游戏零改动陪玩」的验证用例
  （见 `docs/MINIGAME-INTERFACE.md` §12）。
- **智能感知（默认关）**：勾选「工作状态感知」后采集**前台窗口标题与进程名**、空闲时长、
  键鼠活跃度（**只计数，不记录按键、不读文本**；鼠标只计按键与滚轮）与会话锁定 / 屏保，
  判定专注编码 / 与 AI 快速迭代 / 调试 / 阅读 / 离开等工作状态并驱动立绘与台词；
  采集期间才安装低层输入钩子，关闭即卸载。
- **本地 Context API（默认关）**：JSON-RPC 上下文快照与能力清单，仅 `127.0.0.1` / 本机命名管道可访问；
  关闭时不监听任何端口 / 管道。**MCP Client**（外部进程插件，P7.4）、**MCP Server 侧命名管道通道与
  `whalepet-mcp.exe` 桥接 exe**（P7.2）、**DLL 动态插件**（P7.3）与 **ACP / IDE 显式信号**
  （P7.5 / P7.6，可直连 DeepSeek Harness）**均已接入**（见 `docs/ROADMAP-P7-Fin.md`）。
- **回收站清理提醒（默认开）**：以**随机间隔（5–10 分钟）**轮询系统回收站；检测到**非空**时
  切换为 `sweep` 立绘并播报清理提醒（桌宠气泡 + 托盘通知）。采用**边沿触发**（仅「由空变非空」
  时提醒，清空后再次变满会重新提醒），**纯读取、无写入、不联网**；可在右键菜单 / 设置面板关闭。
- **设置面板**：陪伴表现 / 日常·成就·日记 / 小游戏（扫雷 / 找小猫 / 国际象棋 / 接Token）/ 数据与重置。
- **数据本地化**：SQLite 落盘，存储目录三级降级（安装目录 → 用户目录 → 内存）。
- **应用图标**：`assets/icon/whalepet.ico`（同一份用于窗口图标与 `WhalePet.exe` 文件图标）。

> 小游戏规格、难度预设与成就指标见 `docs/MINIGAME-INTERFACE.md`；
> 每日奖励上限沿用参考项目 whale 的「每日 3 局」设计。

---

## 运行要求

- Windows 10 / 11（x64）
- **免安装版**：解压 `dist/WhalePet/` 后双击 `WhalePet.exe`（已自带 Qt 运行库，无需安装 Qt）
- **安装版**：运行 `WhalePet-<版本>-setup.exe`（NSIS 安装向导）

---

## 国际象棋引擎（需自行准备）

> **本程序不自带任何象棋引擎**。小游戏「国际象棋」的对手是一个**外部 UCI 引擎**，
> 需要你自己准备并放入指定目录（引擎是独立第三方程序，**不随本程序分发**）。

### 1. 获取一个 UCI 引擎

任选一个同时满足「Windows x64 + UCI 协议」的引擎，例如开源免费的
[**Stockfish**](https://stockfishchess.org/download/)（下载对应 Windows x64 版本，得到单个
`stockfish*.exe` 可执行文件即可；NNUE 权重已内嵌，无需额外文件）。
注意：本项目不提供任何可用的UCI引擎，如果要测试，请自行准备，或者无视该功能。

### 2. 放入引擎目录

把引擎可执行文件放进 **引擎目录**：

- **免安装版**：`<解压目录>\engine\`（打包脚本会自动创建该空目录）；
- **安装版**：`<安装目录>\engine\`（安装程序会自动创建并授予写权限）。

例如：`<安装目录>\engine\stockfish.exe`。目录名固定为 `engine`；放多个引擎时可在游戏窗口内
用「浏览…」指定用哪一个。

### 3. 在程序内配置

1. 右键 / 托盘菜单 →「小游戏…」→「国际象棋」（或设置面板「小游戏」页的「开始国际象棋」）；
2. 窗口顶部「引擎路径」：
   - **留空 / 未指定**：自动使用 `engine/` 目录下的可执行文件（优先名字含 `stockfish` 者）；
   - **手动指定**：点「浏览…」选择任意位置的引擎 `.exe`（路径会记住，下次沿用）。
3. 「难度」= 引擎棋力档位，共 **6 档**（入门 / 休闲 / 中级 / 高级 / 专家 / 大师）；
  程序按 **搜索深度 3 / 4 / 6 / 8 / 11 / 14** 下发 `go depth`，并附 UCI `Skill Level`
  与思考时间上限（`movetime`，先到者停）——档位越低引擎越弱，**最低档深度为 3**；
  「我执」= 你执白（先手）或执黑（后手），选好后**棋盘会自动把你的一侧摆在下方**，
  对局中也可随时点「翻转棋盘」上下对调。两者都会记住。

### 4. 找不到引擎时

程序不会崩溃：游戏窗口会提示「引擎不可用」并给出指引，鲸鱼娘也会提醒你。请检查
引擎文件是否存在、是否为 Windows x64 可执行文件、以及路径是否配置正确。

> 引擎路径与难度落库在 `settings` 表的 `json_ext`（`chess_engine_path` / `chess_difficulty` /
> `chess_human_is_white`），卸载/重装不丢。

---

## 回收站清理提醒（sweep）

> 立绘激活 18：让此前闲置的 `sweep` 立绘进入真实功能。

**功能说明**

- 后台以**随机间隔（5–10 分钟，每轮重新抽取）**查询系统回收站（`SHQueryRecycleBin`）的条目数与占用；
- 一旦检测到**回收站非空**，鲸鱼娘切换为 `sweep` 立绘并**发送清理提醒**：桌宠气泡播报
  `sweep.remind` 台词，系统托盘同时弹出通知（托盘可用时）。

**使用方式**

- **默认开启**，无需配置；程序启动时立即检查一次，随后进入随机周期；
- 关闭 / 打开：**右键菜单 →「回收站清理提醒」**（勾选项），或
  **设置面板 →「陪伴表现」→「回收站清理提醒（sweep）」**；
- 开关状态落库 `settings.json_ext`（`recycle_bin_reminder_enabled`），重启保留。

**行为描述**

- **边沿触发**：只在「由空（或未知）→ 非空」时提醒一次，避免周期性刷屏；回收站被清空后再次
  变非空会**重新提醒**；
- **只读、无副作用**：仅读取回收站计数，**不删除 / 不移动 / 不清空**任何内容，不联网、不写文件；
- **平台与降级**：查询依赖 Windows Shell API；查询失败或非 Windows 平台一律视为「未知」，
  **不伪造「非空」、不提醒**，且完全不影响其它功能（见 `docs/README.md` §5.3 降级口径）。

---

## 快速上手

| 操作 | 说明 |
|---|---|
| 左键拖拽 | 移动桌宠（松手带惯性滑行）；拖到屏幕边框附近会吸附贴合，并换成该方向的探头立绘 |
| 左键单击 | 摸头 / 摸肚子 / 摸尾巴（按立绘高度分区） |
| 三连击 | 触发「星星」特效 |
| 右键 | 投喂 / 戳一下 / 夸夸 / 回原位 / 状态 / 日常 / 设置 / 小游戏…（悬停或点击展开游戏列表） / 热词录入 / **工作状态感知** / **本地 Context API** / **回收站清理提醒** / 退出 |
| 托盘图标 | 单击或双击切换显示；右键菜单含 显示·隐藏 / 状态 / 日常 / 设置 / 退出 |
| `Ctrl+Alt+K` | 打开「热词录入」面板 |
| 关闭窗口 | 仅隐藏（托盘常驻）；退出请走菜单 / 托盘 |

> **找不到桌宠？** 关闭显示后，托盘菜单与屏幕左下角的「唤回鲸鱼娘」按钮均可恢复；位置被拖出屏幕时会自动夹回可见区域。

---

## 构建（开发者）

### 环境基线

| 组件 | 版本 | 说明 |
|---|---|---|
| Qt | **6.8.4** | 需含 WebP 图像插件与 SQLite 驱动 |
| 编译器 | MSVC（Visual Studio 18 2026） | x64 |
| CMake | **4.4.2** | 项目最低要求 3.21 |
| 生成器 | Visual Studio 18 2026 | 多配置 |

完整环境说明、插件确认与故障排查见 `docs/BUILD.md`。

### 配置与构建

```powershell
# 配置（CMAKE_PREFIX_PATH 指向 Qt 前缀，必填）
& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"

# 构建
& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Debug   --parallel
& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Release --parallel
```

> Release 产物直接生成在 `deploy-release/`（开发部署目录，**含 PDB**，供崩溃分析），见 `docs/BUILD.md` §3。

### 测试

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Debug --output-on-failure --timeout 120
```

目前共 **37 个测试目标**（冒烟 / 状态机 / 台词表 / 数据层 / 养成 / 内容 / 聊天 / 热词 / 设置 /
立绘资源与加载策略 / 扫雷 / 找小猫 / 国际象棋 / 小游戏结算 / 回收站清理提醒 / 插件能力总线 /
感知层骨架 / 真实 Win32 感知 / 工作状态判定 / 游戏记忆 / 游戏记忆端到端 / Unity 适配器 /
RPG Maker 适配器 / 游戏陪伴 / Context API 分发 / 命名管道 + MCP 桥接（P7.2）/ DLL 插件装载（P7.3）/
状态机边界 / Context API HTTP 安全 / ACP 显式信号 / ACP 事件映射 / ACP 客户端 / 外部进程插件 /
宿主服务注册化（P9-A）/ UI 宿主契约与贡献点（P9-C）），
Release / Debug 各 **37/37 passed**（2026-10-07 实测），
策略见 `docs/TESTING.md`。禁止以删除断言、注释用例、放宽比较、吞异常的方式让测试「变绿」。

---

## 打包发布

生成 **免安装版（目录 + zip）** 与 **NSIS 安装包**，产物只落在 `dist/`：

```powershell
# 前置：构建目录必须已存在并已构建（脚本不会新建 / 清理 / 切换构建目录）
#   build-package 以 -DWHALEPET_PACKAGE=ON 配置 → Release 产物落 dist/WhalePet（无调试符号）
powershell -ExecutionPolicy Bypass -File scripts/package-release.ps1 `
    -AppName WhalePet -Version 0.3.0 -BuildDir build-package
```

脚本会：

1. 校验并**复用**既有构建目录（拒绝 Debug 构建目录、**从不新建**构建目录）；
2. 把 `WhalePet.exe` 与 `whalepet-mcp.exe` 暂存到 `dist/WhalePet-<版本>-portable/`；
3. 用 `windeployqt` 补齐 Qt 运行库与插件；
4. 清理任何残留调试文件（`*.pdb` / `*.ilk` / `*.exp` / `*.lib`）；
5. 清空并重建**空的** `engine/` 目录、清掉 `plugins/` 残留（打包机上残留的引擎 / 第三方插件不会被分发），
   并复制 `README.md` / `LICENSE`；
6. 打 zip，并调用 `makensis /INPUTCHARSET UTF8` 生成安装包（`APP_VERSION4` 由脚本按版本补零推导）。

**产出**：

| 产物 | 说明 |
|---|---|
| `dist/WhalePet-<版本>-portable/` | **免安装版目录**（不含调试符号，含**空的** `engine/` 目录） |
| `dist/WhalePet-<版本>-portable.zip` | 免安装版压缩包（直接分发） |
| `dist/WhalePet-<版本>-setup.exe` | **NSIS 安装包**（开始菜单 / 桌面快捷方式 + 卸载程序） |

> `dist/WhalePet/` 只是 CMake 在 `WHALEPET_PACKAGE=ON` 时的 **exe 落点（构建中间产物）**，不是发布
> 产物；发布产物一律带 `-<版本>-` 前缀。命名与台账见 `docs/release.md`。
> 安装包会在安装目录创建 `engine/` 并授予普通用户写权限，卸载时随「删除用户数据」一并清理
> （见 `docs/packages.md` §3.1）。

常用参数：`-BuildDir`（构建目录）、`-Version`、`-QtRoot`（默认 `D:/Qt-debug`）、`-NsisExe`、
`-ExtraExe`（随包二进制，默认 `whalepet-mcp.exe`）、`-SkipZip` / `-SkipInstaller`；
其余（`-ShipFiles` / `-EmptyFolders` / `-PurgeFolders` / `-PurgeDebugFiles`）见脚本头部注释。

> 发布脚本为纯 ASCII（Windows PowerShell 5.1 会把无 BOM 的 UTF-8 脚本按 ANSI 解析）；
> NSIS 脚本为 UTF-8，编译时需 `/INPUTCHARSET UTF8`（已由 `package-release.ps1` 传入）。

---

## 目录结构

```
CMakeLists.txt        构建编排入口（project / 版本 / 语言；WhalePet 0.3.0，C++17），具体配置见 cmake/
cmake/                CMake 模块：CompileOptions / QtDependencies / Libraries / OutputLayout /
                     Executables / Tests / PluginExamples（由顶层 include 引入，详见 docs/BUILD.md §4.1）
CMakePresets.json     Visual Studio x64 Debug / Release 配置预设
src/
  app/           程序入口
  common/        界面共用的视觉资源与 UI 配色
  core/          核心规则与状态机：养成、台词、日常内容、扫雷 / 找小猫 / 国际象棋、工作状态判定（不依赖 Qt Widgets）
  minigame/      小游戏插件接口、注册表、各插件（扫雷 / 找小猫 / 国际象棋）与通用能力总线适配器
  model/         SQLite 数据库、表结构迁移与数据仓储
  view/          Qt Widgets 界面：桌宠窗口、立绘、气泡、状态 / 内容 / 设置 / 热词面板
  viewmodel/     应用编排服务：控制器、养成、聊天、日常内容、小游戏、感知采样与工作状态判定
  platform/      桌面环境感知接口、空实现与 Win32 真实采集（P7；默认关闭，开启后采样）
  plugin/        通用插件总线：能力协议、注册表、内置 / DLL / 外部进程三层装载器（P7；
                 三层均已接入组合根；DLL 层见 P7.3）
  contextapi/    本地 Context API：JSON-RPC 分发、三通道（MCP stdio / 回环 HTTP / 命名管道）、
                 ACP 显式信号与 ACP 客户端（P7.2 / P7.5 / P7.6）
assets/
  icon/          应用图标
  lines/         台词语料（普通 / 问候 / 羁绊 / 梗 / 游戏 / 找小猫 / 国际象棋 / 工作状态）
  maps/          找小猫的物体列表与地图（外部可配置）
  poses/         93 张 WebP 立绘（含 4 张桌面贴边探头立绘）
resources/qt-ui/ 全局 Qt 样式表及资源清单
scripts/       发布脚本（package-release.ps1）、工作区初始化（init-*.ps1）与 NSIS 安装脚本（installer.nsi）
docs/            设计文档索引、构建 / 测试说明、路线图与踩坑记录
tests/           Qt6::Test 测试源码（37 个测试目标）
dummy/stockfish/ 本地测试用的 Stockfish 引擎（不随包分发）
references/      参考项目资料
```

> 发行目录（`dist/WhalePet/` 与安装目录）内会由打包脚本 / 安装程序创建**空的** `engine/`
> 目录，作为用户自备象棋引擎的落点（见「国际象棋引擎」）。

---

## 数据存储

- 默认写入 **`WhalePet.exe` 同级 `data/whalepet.db`**；
- 安装目录不可写时降级到用户目录，再不可用则降级为内存库（日志告警，不崩溃）；
- 卸载时可选择**保留**或删除 `data/`（存档）。

---

## 文档

`docs/README.md` 是设计文档索引，包含：

- 架构与分层、状态机、立绘表现、数据模型、玩法、聊天、设置、测试策略；
- 分阶段路线图 `ROADMAP-Pn(-Fin).md` 与各阶段真实踩坑记录 `docs/pitfalls/`；
- 构建基线 `docs/BUILD.md`（含 WebP / SQLite 插件确认与常见失败排查）；
- 插件化架构与本地 Context API：`PLUGIN-ARCHITECTURE.md`、`CONTEXT-API.md`、`ACP-EVAL.md`；
- **P7.2（命名管道 / 桥接 exe）与 P7.3（DLL 插件）的逐项交付核查**：
  `docs/P7-REMAINING-INTERFACES-AUDIT.md`。

---

## 许可

本项目以 **MIT License** 发布，见 `LICENSE`。
立绘资产与部分玩法 / 聊天逻辑移植自开源参考项目，复用边界见 `docs/README.md` §四。
