# WhalePet · 鲸鱼娘桌宠

> 基于 **Qt 6 原生底座** 的 Windows 独立桌面宠物：复用「鲸鱼娘」立绘与 whale 的养成 / 梗聊天逻辑，
> **不依赖任何宿主程序**，双击即用。
>
> 当前版本：**0.2.0**

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
  走迷宫，绕过礁石、捡起沿途物件（扇贝 / 海龟 / 海星 / 珍珠 / 海草 / 水母 / 漂流瓶 / 破靴子），
  顺着海流切换场景，在最深处找到小猫即通关；不同类别物体触发差异化立绘与**专属台词**；
  难度决定需要穿越 1 / 2 / 3 个场景。**物体列表、地图与台词均为外部资源**（`assets/maps/`、
  `assets/lines/kitten.txt`），可自行增删替换。
- **智能感知（默认关）**：勾选「工作状态感知」后采集**前台窗口标题与进程名**、空闲时长、
  键鼠活跃度（**只计数，不记录按键、不读文本**；鼠标只计按键与滚轮）与会话锁定 / 屏保，
  判定专注编码 / 与 AI 快速迭代 / 调试 / 阅读 / 离开等工作状态并驱动立绘与台词；
  采集期间才安装低层输入钩子，关闭即卸载。
- **本地 Context API（默认关）**：JSON-RPC 上下文快照与能力清单，仅 `127.0.0.1` 可访问；
  关闭时不监听任何端口。（MCP / ACP 为**预留接口**，暂不接入。）
- **设置面板**：陪伴表现 / 日常·成就·日记 / 小游戏（扫雷 / 找小猫）/ 数据与重置。
- **数据本地化**：SQLite 落盘，存储目录三级降级（安装目录 → 用户目录 → 内存）。
- **应用图标**：`assets/icon/whalepet.ico`（同一份用于窗口图标与 `WhalePet.exe` 文件图标）。

> 小游戏规格、难度预设与成就指标见 `docs/MINIGAME-INTERFACE.md`；
> 每日奖励上限沿用参考项目 whale 的「每日 3 局」设计。

---

## 运行要求

- Windows 10 / 11（x64）
- **免安装版**：解压 `dist/WhalePet/` 后双击 `WhalePet.exe`（已自带 Qt 运行库，无需安装 Qt）
- **安装版**：运行 `WhalePet-Setup-<版本>.exe`（NSIS 安装向导）

---

## 快速上手

| 操作 | 说明 |
|---|---|
| 左键拖拽 | 移动桌宠（松手带惯性滑行）；拖到屏幕边框附近会吸附贴合，并换成该方向的探头立绘 |
| 左键单击 | 摸头 / 摸肚子 / 摸尾巴（按立绘高度分区） |
| 三连击 | 触发「星星」特效 |
| 右键 | 投喂 / 戳一下 / 夸夸 / 回原位 / 状态 / 日常 / 设置 / 小游戏…（悬停或点击展开游戏列表） / 热词录入 / **工作状态感知** / **本地 Context API** / 退出 |
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

目前共 **17 个测试目标**（冒烟 / 状态机 / 台词表 / 数据层 / 养成 / 内容 / 聊天 / 热词 / 设置 / 扫雷 / 找小猫 /
小游戏结算 / 插件能力总线 / 感知层骨架 / 真实 Win32 感知 / 工作状态判定 / Context API 分发），
策略见 `docs/TESTING.md`。禁止以删除断言、注释用例、放宽比较、吞异常的方式让测试「变绿」。

---

## 打包发布

一键生成 **免安装版** 与 **NSIS 安装包**：

```powershell
powershell -ExecutionPolicy Bypass -File packaging/make-package.ps1
```

脚本会：

1. 以 `-DWHALEPET_PACKAGE=ON` 配置**独立**构建目录 `build-package`，Release 产物落在 `dist/WhalePet`
   且**不生成调试符号**；
2. 构建 Release；
3. 用 `windeployqt` 补齐 Qt 运行库与插件；
4. 清理任何残留调试文件（`*.pdb` / `*.ilk` / `*.exp` / `*.lib`）；
5. 调用 `makensis` 生成安装包。

**产出**：

| 产物 | 说明 |
|---|---|
| `dist/WhalePet/` | **免安装版**（不含调试符号，zip 后即可分发） |
| `dist/WhalePet-Setup-<版本>.exe` | **NSIS 安装包**（开始菜单 / 桌面快捷方式 + 卸载程序） |

可调参数：

```powershell
powershell -ExecutionPolicy Bypass -File packaging/make-package.ps1 `
    -QtDir  "D:/Qt-debug" `
    -NsisDir "D:\program files (x86)\NSIS" `
    -Version "0.2.0"
```

> 打包脚本为纯 ASCII（Windows PowerShell 5.1 会把无 BOM 的 UTF-8 脚本按 ANSI 解析）；
> NSIS 脚本为 UTF-8，编译时需 `/INPUTCHARSET UTF8`（脚本内已由 `make-package.ps1` 传入）。

---

## 目录结构

```
CMakeLists.txt        CMake 工程定义（WhalePet 0.2.0，C++17）
CMakePresets.json     Visual Studio x64 Debug / Release 配置预设
src/
  app/           程序入口
  common/        界面共用的视觉资源与 UI 配色
  core/          核心规则与状态机：养成、台词、日常内容、扫雷与找小猫、工作状态判定（不依赖 Qt Widgets）
  minigame/      小游戏插件接口、注册表、各插件（扫雷 / 找小猫）与通用能力总线适配器
  model/         SQLite 数据库、表结构迁移与数据仓储
  view/          Qt Widgets 界面：桌宠窗口、立绘、气泡、状态 / 内容 / 设置 / 热词面板
  viewmodel/     应用编排服务：控制器、养成、聊天、日常内容、小游戏、感知采样与工作状态判定
  platform/      桌面环境感知接口、空实现与 Win32 真实采集（P7；默认关闭，开启后采样）
  plugin/        通用插件总线：能力协议、注册表、内置 / DLL / 外部进程三层装载器（P7）
  contextapi/    本地 Context API：JSON-RPC 分发、MCP stdio / 回环 HTTP 双通道、ACP 预留接口（P7）
assets/
  icon/          应用图标
  lines/         台词语料（普通 / 问候 / 羁绊 / 梗 / 游戏 / 找小猫 / 工作状态）
  maps/          找小猫的物体列表与地图（外部可配置）
  poses/         93 张 WebP 立绘（含 4 张桌面贴边探头立绘）
resources/qt-ui/ 全局 Qt 样式表及资源清单
packaging/       打包脚本（make-package.ps1）与 NSIS 安装脚本（whalepet.nsi）
docs/            设计文档索引、构建 / 测试说明、路线图与踩坑记录
tests/           Qt6::Test 测试源码（17 个测试目标）
referances/      参考项目资料
```

---

## 数据存储

- 默认写入 **`WhalePet.exe` 同级 `data/whalepet.db`**；
- 安装目录不可写时降级到用户目录，再不可用则降级为内存库（日志告警，不崩溃）；
- 卸载时可选择**保留**或删除 `data/`（存档）。

---

## 文档

`docs/README.md` 是设计文档索引，包含：

- 架构与分层、状态机、立绘表现、数据模型、玩法、聊天、设置、测试策略；
- 分阶段路线图 `ROADMAP-Pn(-Fin).md` 与各阶段真实踩坑记录 `traps-Pn.md`；
- 构建基线 `docs/BUILD.md`（含 WebP / SQLite 插件确认与常见失败排查）。

---

## 许可

本项目以 **MIT License** 发布，见 `LICENSE`。
立绘资产与部分玩法 / 聊天逻辑移植自开源参考项目，复用边界见 `docs/README.md` §四。
