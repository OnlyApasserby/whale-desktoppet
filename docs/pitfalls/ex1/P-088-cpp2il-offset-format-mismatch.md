# Cpp2IL 的 `diffable-cs` 偏移注释格式与 `UnityDumpConverter` 不兼容（SOP 误称其产出 `dump.cs`）

> **原编号**：`TRAP-EX1-009`　**阶段**：EX1　**来源**：EX1.5 真机验收（2026-10-07）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/pitfalls/index.md`。

---

- **现象（可复现步骤 / 报错原文）**：
  1. 按 `docs/UNITY-SOP.md` §2「使用 Il2CppDumper（或 Cpp2IL）产出 `dump.cs`」，对 IL2CPP 目标执行：
     ```
     Cpp2IL.exe --game-path "F:\learn\The Piper Of Dawn" --exe-name ThePiper \
                --output-as diffable-cs --output-to <tmp>
     ```
     （Cpp2IL 版本 `2022.1.0+b5ad444b...`；`--list-output-formats` 实为
     `dummydll / dll_default / dll_empty / dll_throw_null / dll_il_recovery / diffable-cs / isil / wasmmappings / wasm_name_section`）。
  2. 反编译**成功**，但产物**不是单个 `dump.cs`**，而是 `DiffableCs/<Assembly>/<Namespace>/<Class>.cs` 的**按类拆分**文本。
  3. 字段偏移注释格式为 **`//Field offset: 0x10`**（`//` 后直接接 `Field`），例如实测原文：
     ```
     public byte[] TypesData; //Field offset: 0x8
     public int TotalTypes; //Field offset: 0x10
     ```
  4. 对全量产物检索 `// 0x…`（`UnityDumpConverter::fieldRe` 所需格式）→ **匹配数 0**。
     即 `UnityDumpConverter::parseFieldOffsets()` 必然失败并原文报：
     ```
     dump.cs 中未识别到任何带偏移（// 0x…）的字段
     ```
  5. 因此 `UnityDumpConverter::buildProfile()` 无法据此产出 profile。
- **根因**：`UnityDumpConverter` 的字段正则按 **Il2CppDumper** 的 `dump.cs` 约定编写
  （`src/gamestate/UnityDumpConverter.cpp:31-36`：`^\s*(.*?)([A-Za-z_]\w*)\s*;\s*//\s*0x([0-9A-Fa-f]+)`），
  而 `docs/UNITY-SOP.md` §2 把 **Cpp2IL** 一并列为「产出 `dump.cs`」的来源——二者实际输出格式不同：
  Cpp2IL 无 `dump.cs` 输出格式，其 C# 文本用 `//Field offset:` 且按类拆分。
  > 另：该目标程序集经 **OPS.Obfuscator** 混淆（`Piper.Alchemy.Logic` 下类/字段名为 `epd.chqf`、
  > `epa.chpw` 等），无 `hp / gold / level` 等语义字段，故**即使**解决格式问题也无法离线确定约定字段。
- **解决或规避**：
  - 短期：改用 **Il2CppDumper** 产出 `dump.cs`（其格式与本转换器一致）；
  - 或把 Cpp2IL `diffable-cs` **归一化**为 `dump.cs`：将 `//Field offset: 0x..` 改写为 `; // 0x..`、
    并补 `// Namespace: <ns>` 头与类声明（一次性转换脚本，不改产品代码）；
  - 长期（待确认）：修订 `docs/UNITY-SOP.md` §2 澄清 Cpp2IL 实际产物形态，
    或为 `UnityDumpConverter` 增加 `//Field offset:` 变体解析（须同步补 `test_unity_adapters` 夹具）。
  - 混淆目标：约定字段只能由**离线 CE 运行期定位**（人工步骤）后手写 profile，不在本工具自动能力内。
- **影响与关联文档**：`src/gamestate/UnityDumpConverter.cpp:29-36`（`fieldRe`）、
  `src/gamestate/UnityDumpConverter.cpp:59-138`（`parseFieldOffsets`）、`tests/test_unity_adapters.cpp`（夹具用 `// 0x..`）、
  `docs/UNITY-SOP.md` §2/§3、`docs/ROADMAP-ex1.md` EX1.2 / EX1.5。
