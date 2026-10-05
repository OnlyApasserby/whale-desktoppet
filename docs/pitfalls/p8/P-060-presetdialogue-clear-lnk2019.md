# `PresetDialogueTable::clear()` 只声明未定义 → LNK2019

> **原编号**：`TRAP-P8-002`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：链接 ｜ **影响**：`WhalePet.exe` 链接失败（Debug / Release 同）。

### 现象

```
whalepet_view.lib(DialogueService.obj) : error LNK2019: 无法解析的外部符号
  "public: void __cdecl whalepet::core::PresetDialogueTable::clear(void)"
F:\develop\desktoppet\build\Debug\WhalePet.exe : fatal error LNK1120: 1 个无法解析的外部命令
```

### 根因

`PresetDialogue.h` 声明了 `void clear();`，但 `PresetDialogue.cpp` 漏写了实现
（`loadFromText` / `addAnswer` / `find` / `countOf` 都写了，唯独漏掉 `clear`）。
头文件里以 `inline` 方式写的 `empty()` / `size()` 掩盖了同类风险，只有非内联声明会暴露。

### 解决

在 `PresetDialogue.cpp` 补 `void PresetDialogueTable::clear() { m_questions.clear(); }`。

### 影响与关联文档

`src/core/PresetDialogue.{h,cpp}`；见 `DIALOGUE.md` §2。

---
