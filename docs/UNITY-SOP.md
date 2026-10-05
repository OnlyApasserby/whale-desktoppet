# Unity 游戏接入 SOP（EX1.2）

> 归属：`docs/ROADMAP-ex1.md` §2.6.1、EX1.2。
> 适用：Unity `Mono` / `IL2CPP` 后端**单机**游戏。
> **红线**：全程**只读**。不注入、不 hook、不使用 BepInEx/联机/带反作弊的游戏；
> 不向目标进程写入任何字节，不向其注入或执行代码。

---

## 0. 一句话流程

判定后端 → 用官方 dump 工具产出 `dump.cs` → 用 `UnityDumpConverter` 把
「类名.字段名」映射成 profile（偏移不再硬编码）→ 运行期由
`UnityMonoAdapter` / `UnityIl2CppAdapter` 只读消费 profile。

---

## 1. 判定后端（Mono 还是 IL2CPP）

用任务管理器/Process Explorer 查看目标进程的已加载模块，或用本项目的探测工具：

| 现象 | 结论 | 适配器 | 默认原生模块名 |
|---|---|---|---|
| 加载 `mono-2.0-bdwgc.dll` 或 `mono.dll` | **Mono** | `UnityMonoAdapter` | `mono-2.0-bdwgc.dll`（旧版 `mono.dll`） |
| 加载 `GameAssembly.dll` | **IL2CPP** | `UnityIl2CppAdapter` | `GameAssembly.dll` |
| 有 `UnityPlayer.dll` 但两者都无 | 引擎版本异常/加壳 | 见 §5 降级 | — |

代码侧等价探测（只读，仅查询模块是否加载）：

```cpp
gamestate::UnityBackend backend = gamestate::detectUnityBackend(&reader);
// backend.engine == "unity-mono" | "unity-il2cpp" | ""（未命中）
```

---

## 2. 产出 `dump.cs`

**IL2CPP**：使用 `Il2CppDumper`（或 `Cpp2IL`）。需要 `GameAssembly.dll` +
`global-metadata.dat`（通常在 `*_Data/il2cpp_data/Metadata/`）。产出 `dump.cs`。

**Mono**：使用 `Cpp2IL`（支持 Mono 与 IL2CPP）产出 `dump.cs`；
或直接用 dnSpy/ILSpy 查**类与字段名**（偏移仍需 §3 从运行时定位）。

`dump.cs` 中字段以行尾注释给出偏移，形如：

```csharp
// Namespace: Game
public class Player : MonoBehaviour // TypeDefIndex: 100
{
	// Fields
	public int hp; // 0x10
	public int gold; // 0x14
	public static float posX; // 0x18
}

// Namespace: 
public class Monster // TypeDefIndex: 200
{
	// Fields
	public int level; // 0x30
}
```

> 解析规则：取分号前**最后一个标识符**为字段名，`// 0x…` 为字段偏移；
> 类名可用「命名空间.类」或「简单类名」两种写法定位（同名简单类跨命名空间时后者会覆盖，需用全名）。

---

## 3. 构造 profile（离线，`UnityDumpConverter`）

```cpp
using namespace whalepet::gamestate;

const QString dump = readAllText("dump.cs"); // Il2CppDumper / Cpp2IL 产物

UnityProfileSpec spec;
spec.engine          = "unity-mono";          // 或 "unity-il2cpp"
spec.process         = "Game.exe";
// spec.module 留空 → 按 engine 取默认原生模块名（见 §1 表格）
spec.staticBaseOffset = 0x0;                  // 见下：静态字段数据区基址 = moduleBase + 本值
spec.validation.maxJumps = 4;                 // 可选：魔数校验
spec.mappings = {
    { "hp",    "int32", "Game.Player", "hp",    "high", {} },
    { "gold",  "int32", "Game.Player", "gold",  "mid",  {} },
    { "posX",  "float", "Game.Player", "posX",  "high", {} },
    { "level", "int32", "Monster",     "level", "high", {} },
};

GameProfile profile;
QString error;
if (!UnityDumpConverter::buildProfile(dump, spec, &profile, &error)) {
    // 名字未命中会明确报「类.字段 未找到」，不产出半成品
}
```

`UnityFieldMapping` 各列：`{ 逻辑字段名, kind, 类名, 字段名, freq, tail }`。
`tail` 用于「实例字段」：字段偏移之后追加的跳转/偏移（如先解引用静态单例再取实例成员）。

### 静态字段数据区基址（`staticBaseOffset`）怎么来？

- **IL2CPP**：静态字段实际不在 `GameAssembly.dll` 的固定 RVA，而在某 `Il2CppClass.static_fields`
  指向的堆区。用运行期观测（CE 附加、只读遍历 `Il2CppClass`）拿到该基址相对
  `GameAssembly.dll` 模块基址的偏移，填入 `spec.staticBaseOffset`。
- **Mono**：静态字段数据由 `mono_vtable_get_static_field_data` 得到，其所在区域基址同理填入。

> `dump.cs` 的 `// 0x…` 是**类内偏移**；最终地址 = `moduleBase + staticBaseOffset + 字段偏移`
> （本项目 profile 语义：`staticRoot = moduleBase + moduleBaseOffset`，`fields[].chain[0]` 即字段偏移）。

---

## 4. 运行期集成

```cpp
std::unique_ptr<gamestate::IGameStateAdapter> adapter =
    gamestate::createGameStateAdapter(profile, &error);   // 按 engine 路由到具体适配器
adapter->attach(profile, &error);
core::GameSample sample;
if (!adapter->read(&sample, &error)) {
    // sample.available == false；error 含原因（含后端提示），**绝不伪造数据**
}
```

- **不缓存托管对象地址**：`ChainSampler` 每轮从静态根重走指针链，避免 GC 移动后读到陈旧地址。
- 连续失败 ≥ `kGameInvalidateAfterFailures` 次 → 档案失效（`adapter->invalidated()`），
  须显式重连/重载后 `reset` 再采样（§4.3）。

---

## 5. 元数据加密 / 无法解析：优雅降级

| 情况 | 行为 |
|---|---|
| `dump.cs` 解析不出字段 | `parseFieldOffsets` 报「未识别到任何带偏移的字段」 |
| 映射的类/字段不在 `dump.cs` | `buildProfile` 报「类.字段 未找到」（不产出档案） |
| 目标未加载预期模块（后端选错） | `read` 返回 `available=false`，原因附带引擎提示（Mono→试 `GameAssembly.dll` 的说法相反亦然） |
| `dump.cs`/`global-metadata` 加密或裁剪 | 改用 CE 等**只读**手段直接定位偏移，手写 profile 走 `engine=generic`（通用指针链底座） |

**始终不崩溃**：所有解析/读取失败均以 `bool` + `error` 表达，`available=false`。

---

## 6. 验收与排查

离线自动化（本仓库）：

```
cmake --build build --config Debug --target whalepet_gamestate test_unity_adapters --parallel
ctest --test-dir build -C Debug -R test_unity_adapters --output-on-failure
```

真机人工验收（EX1.2 验收标准）：

- [ ] Mono 单机、IL2CPP 单机各 1 款，端到端读出约定字段。
- [ ] GC 移动场景下连续读取 10 分钟地址不失效。
- [ ] 元数据加密/无法解析时优雅降级（§5）。

排查清单：

1. `detectUnityBackend` 结果与 `profile.engine` 是否一致（不一致会被 `attach` 拒绝）。
2. `profile.module` 是否为目标进程真实加载的模块名（大小写不敏感）。
3. `staticBaseOffset` 是否随游戏重启变化（应为模块内**稳定**偏移）。
4. 偏移是否随版本/补丁变动（换版本须重新产出 profile）。

---

## 7. 引用

- `src/gamestate/UnityRuntime.{h,cpp}`：后端判定与模块命名。
- `src/gamestate/UnityDumpConverter.{h,cpp}`：`dump.cs` → profile。
- `src/gamestate/UnityMonoAdapter.{h,cpp}`、`UnityIl2CppAdapter.{h,cpp}`、`UnityAdapterBase.{h,cpp}`。
- `tests/test_unity_adapters.cpp`：离线验收测试。
- 相关踩坑：`docs/pitfalls/`（TRAP-EX1-003 / 004）。
