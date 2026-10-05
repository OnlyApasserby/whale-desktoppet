# JSON 数字是 `double` ⇒ 小数静默截断、超范围值触发未定义行为

> **原编号**：`TRAP-P7-015`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤）**：`ProfileLoader::loadFromJson` 处理
  `{"engine":"generic",…,"fields":[{"name":"hp","kind":"int32","chain":[1.5]}]}`
  竟然**加载成功**并把偏移写成 `1`（静默改写档案）；
  `chain: [1e30]` 与 `maxBytesPerRound: 1e30` 则进入
  `static_cast<std::uint64_t>(double)`——C++ 规定该转换在值域外是**未定义行为**。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2，Debug/Release；
  `src/gamestate/GameProfile.cpp`。
- **根因**：`QJsonDocument` 把所有 JSON 数字统一存成 `double`，
  原 `parseU64()` 只判了 `d < 0.0` 就直接转换，既没判「是不是整数」，
  也没判「是否还在 `uint64` 可表示范围内」；`maxBytesPerRound` 与
  `validation.maxJumps` 同理（后者用 `QJsonValue::toInt(4)`，超范围时**静默回落默认值**，
  等于悄悄改了链深度上限）。
- **解决或规避**：`parseU64` 增补「有限性（NaN 一并落负分支）→ 必须为整数
  （`d != std::floor(d)` 即拒）→ 小于 2^64（以 `2^64` 本身为拒界）」三道判定；
  `maxBytesPerRound` 与 `maxJumps` 增补整数性与上限校验，
  上限取 `kMaxProfileBytesPerRound = 1 MiB`（默认仅 4 KiB，留 250 倍余量）与
  `kMaxProfileJumps = 16`（真实档案 ≤ 4 级）。**越界一律拒绝，不静默回落默认值。**
- **影响与关联文档**：`src/gamestate/GameProfile.{h,cpp}`；
  `tests/test_gamestate_boundaries.cpp` 的 `profileRejectsOutOfRangeNumbers`
  （含「上限边界本身允许」的正例，避免只测拒绝侧）。
