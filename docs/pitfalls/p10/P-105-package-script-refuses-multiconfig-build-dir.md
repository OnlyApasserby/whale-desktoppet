# P-105 · 打包脚本把 VS 多配置构建目录误判为 Debug，构建目录固定为 `build/` 后必然拒跑

| 字段 | 取值 |
|---|---|
| 阶段 | P10（工作能力插件；第一阶段：构建与部署目录收敛） |
| 原编号 | —（P10 新增） |
| 模块 | `scripts/package-release.ps1` |
| 报错分层 | script（发布脚本运行期，第 1 步校验） |
| 严重度 | major（完全阻断发布打包链） |

## 一、现象（可复现步骤 / 报错原文）

复现步骤：

1. 用与 `CMakePresets.json` 的 `vs-debug` 相同取值配置既有构建目录：
   `cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"`；
2. 复用该目录执行发布脚本：
   `scripts\package-release.ps1 -AppName WhalePet -Version 0.3.0`（旧版可显式传 `-BuildDir build`）。

**期望**：脚本复用 `build/`，进入暂存 → `windeployqt` → 打 zip → `makensis` 流程。

**实际**：脚本第 1 步即抛错退出：

```
Refused: build is a Debug build directory. Releases require a Release build.
```

证据（真实缓存内容，`Select-String` 实测）：

```
build\CMakeCache.txt:84     : CMAKE_GENERATOR:STRING=Visual Studio 18 2026
build\CMakeCache.txt        : 含 CMAKE_BUILD_TYPE:STRING=Debug
build-package\CMakeCache.txt:365 : CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026
build-package\CMakeCache.txt     : 无 CMAKE_BUILD_TYPE 行
```

旧判定命中路径：

```powershell
if ($cacheText -match 'CMAKE_BUILD_TYPE:STRING=Debug') { throw ... }
```

## 二、根因

1. **VS 多配置生成器忽略 `CMAKE_BUILD_TYPE`**（配置由 `--config` 决定），但 `CMakePresets.json`
   的 `vs-debug` 预设仍把它写进缓存。对 VS 目录而言该键**不含「本目录只产出 Debug」的语义**；
   旧判定按单配置目录（Ninja / Makefiles）的口径处理，于是把 `build/` 误判为 Debug 专目录。
2. **第二层坑（顺带发现）**：`CMAKE_GENERATOR` 缓存项**类型不稳定**——`build/` 里写作
   `STRING`，`build-package/` 里写作 `INTERNAL`。若用
   `CMAKE_GENERATOR:INTERNAL=Visual Studio` 精确匹配来放行，同样会在 `build/` 上漏判。
3. 该缺陷在「双构建目录」时期被掩盖：`build-package/`（打包专用）此前由手工
   `cmake -DWHALEPET_PACKAGE=ON` 配置，缓存里**没有** `CMAKE_BUILD_TYPE` 行，因此从未触发。
   按用户要求把构建目录**固定为 `build/`**（删除 `build-package/`、`deploy-release/`）后，
   该误判 100% 命中，发布打包完全不可用。

## 三、解决或规避

`scripts/package-release.ps1` 第 1 步改为**先判生成器、再判配置**，且不依赖缓存项类型：

```powershell
if ($cacheText -notmatch 'CMAKE_GENERATOR[^=]*=\s*(Visual Studio|Xcode|Ninja Multi-Config)') {
    if ($cacheText -match 'CMAKE_BUILD_TYPE:STRING=Debug') {
        throw ('Refused: ' + $BuildDir + ' is a Debug build directory. Releases require a Release build.')
    }
}
```

即：**多配置生成器豁免，单配置生成器仍拒绝 Debug 目录**——既保留原防误用能力，又不误伤 `build/`。

## 四、影响与关联文档

- 影响面：发布打包链（全量回归的必跑环节）；未解决时「脚本固定复用 `build`」这一要求无法落地。
- 关联：`docs/BUILD.md` §10、`docs/TESTING.md` §8.1 / §8.2、`docs/packages.md` §1、
  `docs/release.md` §1.1、`docs/pitfalls/index.md`（阶段表）。
- 本轮回归结果：Debug / Release CTest 各 **35/35**；脚本复用 `build`、主程序定位到
  `build\Release\WhalePet.exe`，三产物重出并通过 §8.2 第 1～9 项（第 10 项待人工验收）。
