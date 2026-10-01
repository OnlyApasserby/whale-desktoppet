# WhalePet · 鲸鱼娘桌宠

> 基于 **Qt 6 原生底座** 的 Windows 独立桌面宠物：复用「鲸鱼娘」立绘与 whale 的养成 / 梗聊天逻辑，
> **不依赖任何宿主程序**，双击即用。

---

## 功能特性

- **桌面常驻**：透明 + 无边框 + 置顶窗口；拖拽移动（松手有惯性滑行）、按高度分区点击反馈（头 / 肚 / 尾）。
- **养成系统**：等级 / 经验 / 心情 / 好感 / 饱食 / 羁绊 / 累计陪伴 / 连续签到。
- **日常内容**：每日任务、周签到、**39 项成就墙**、成长日记。
- **梗聊天**：分时问候、心情分层台词、羁绊 Lv3/5/7 专属台词；关键词梗表情感知（**默认关**）。
- **自定义热词**：全局热键 `Ctrl+Alt+K` 录入「热词 → 表情」，优先级按录入顺序（详见 `docs/CHAT.md`）。
- **设置面板**：陪伴表现 / 日常·成就·日记 / 小游戏（预留）/ 数据与重置。
- **数据本地化**：SQLite 落盘，存储目录三级降级（安装目录 → 用户目录 → 内存）。

> 「戳泡泡」小游戏**本期不实现**，仅预留接口，见 `docs/MINIGAME-INTERFACE.md`。

---

## 运行要求

- Windows 10 / 11（x64）
- **免安装版**：解压 `dist/WhalePet/` 后双击 `WhalePet.exe`（已自带 Qt 运行库，无需安装 Qt）
- **安装版**：运行 `WhalePet-Setup-<版本>.exe`（NSIS 安装向导）

---

## 快速上手

| 操作 | 说明 |
|---|---|
| 左键拖拽 | 移动桌宠（松手带惯性滑行） |
| 左键单击 | 摸头 / 摸肚子 / 摸尾巴（按立绘高度分区） |
| 三连击 | 触发「星星」特效 |
| 右键 | 投喂 / 戳一下 / 夸夸 / 回原位 / 状态 / 日常 / 设置 / 热词录入 / 退出 |
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

目前共 **9 个测试目标**（状态机 / 台词表 / 数据层 / 养成 / 内容 / 聊天 / 热词 / 设置 / 冒烟），
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
    -Version "0.1.0"
```

> 打包脚本为纯 ASCII（Windows PowerShell 5.1 会把无 BOM 的 UTF-8 脚本按 ANSI 解析）；
> NSIS 脚本为 UTF-8，编译时需 `/INPUTCHARSET UTF8`（脚本内已由 `make-package.ps1` 传入）。

---

## 目录结构

```
src/
  app/         程序入口（样式表注入 + 主窗口启动）
  core/        纯逻辑层：状态机 / 养成规则 / 台词表 / 成就 / 任务 / 签到（**零 Qt 依赖**，可脱界面单测）
  model/       数据层：SQLite 连接 / 建表迁移 / 各仓储（Qt6::Core + Sql，不链接 Widgets）
  view/        Qt Widgets 界面：主窗口 / 立绘渲染 / 台词气泡 / 状态·内容·设置·热词面板
  viewmodel/   编排服务：控制器 / 养成 / 聊天 / 成就 / 任务 / 签到
assets/        92 张立绘（webp）+ 台词语料（lines / greet / bond / meme）
resources/     全局样式表（qt-ui：default.qss + project.qss，黑底白字）
packaging/     打包脚本（make-package.ps1）与 NSIS 脚本（whalepet.nsi）
docs/          设计文档与分阶段路线图 / 踩坑记录
tests/         Qt6::Test 自研测试
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
