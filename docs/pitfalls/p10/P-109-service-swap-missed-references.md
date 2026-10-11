# P-109 · 用新服务替换旧服务时漏改宿主引用，编译分三轮才收敛（C1083 / C2065 / C3861）

| 字段 | 取值 |
|---|---|
| 阶段 | P10（工作能力插件；④ 彩蛋迁移 / 服务换位） |
| 原编号 | —（P10 新增） |
| 模块 | `src/view/PetWindow.{h,cpp}`、`src/viewmodel/builtin/BuiltinServicePlugins.*` |
| 报错分层 | build（编译期，逐轮暴露） |
| 严重度 | minor（编译期即暴露；无错误产物进入运行 / 打包） |

## 一、现象（可复现步骤 / 报错原文）

P10 把「戳一戳彩蛋」迁移进工作台后，删除 `viewmodel::EasterEggService`（服务 + builtin 插件），
以 `viewmodel::WorkService` 取代其位。**一次只改一处引用**，于是 Debug 构建分三轮报错：

```text
# 第 1 轮
F:\develop\desktoppet\src\view\PetWindow.h(132,58): error C2065: “m_work”: 未声明的标识符
F:\develop\desktoppet\src\view\PetWindow.cpp(44,10): error C1083: 无法打开包括文件:
    “viewmodel/EasterEggService.h”: No such file or directory

# 第 2 轮
F:\develop\desktoppet\src\view\PetWindow.cpp(1021,5): error C3861: “applyCodeEggSettings”: 找不到标识符
```

（`m_work` 的声明、访问器改名、前向声明 `class WorkService;`、include 替换、装配函数与设置生效
调用点，共 6 处；逐轮改一处即逐轮失败一次。）

## 二、根因

「服务换位」是**跨文件原子改动**：一个符号在宿主侧至少有 6 类引用点——
① 成员声明 ② 公开访问器 ③ 前向声明 ④ `#include` ⑤ 装配函数（setup*）定义与调用 ⑥ 设置生效
函数与调用点；此外还有 `cmake/Libraries.cmake`、`BuiltinServicePlugins.*`、测试与文档。
删除旧服务后，这些点**任何一处不改都会编译失败**，逐轮修复只是把一次原子改动拆成了多次失败。

## 三、解决或规避

一次性全量 grep 旧符号后逐处替换，改动清单（本轮实际执行）：

| # | 位置 | 改动 |
|---|---|---|
| 1 | `PetWindow.h` | 前向声明 `class EasterEggService;` → `class WorkService;`（按字母序） |
| 2 | `PetWindow.h` | 访问器 `easterEggService()` → `workService()`；成员 `m_easterEgg` → `m_work` |
| 3 | `PetWindow.h/.cpp` | 装配 / 设置函数 `setupEasterEgg` / `applyCodeEggSettings` → `setupWork` / `applyWorkSettings` |
| 4 | `PetWindow.cpp` | `#include "viewmodel/EasterEggService.h"` → `"viewmodel/WorkService.h"`；构造序与设置生效调用点同步 |
| 5 | `BuiltinServicePlugins.{h,cpp}` | 句柄字段、能力 id、注册项、能力 id 清单（注册数仍为 6） |
| 6 | `cmake/Libraries.cmake`、`cmake/Tests.cmake`、`tests/test_service_plugins.cpp` | 源清单 / 目标清单 / id 与句柄断言 |

修后 Debug / Release 构建退出码 0，CTest 各 **36/36**。

**规避口径**：删除或替换**被宿主引用的服务**前，先 `grep -n "<旧符号>" src tests cmake`（含成员、
访问器、前向声明、include、setup*、能力 id、测试断言、CMake 清单）**一次性收集全部引用点再动手**；
此类改动适合作为一次独立提交（一个关注点），不要与功能改动混在一起。

## 四、影响与关联文档

- 影响面：仅编译期；未产生错误产物，未进入发布链。
- 关联：`docs/pitfalls/p10/P-108-*`（同日同类「漏 include」）、`docs/pitfalls/ex3/P-092-*`
  （移除目标后旧构建目录的增量告警，同属「删除后残留引用」家族）、`docs/ARCHITECTURE.md` 附录 D.2。
