# 指针链地址加法无溢出检查 ⇒ 野指针 + 偏移绕回，读到无关内存

> **原编号**：`TRAP-P7-019`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：`chain = [0x100, 0x200]` 且第一跳指针为 `0xFFFFFFFFFFFFFF00` 时，
  `address = pointer + chain[i]` **回绕**成 `0x100`，后续读取的是模块起始处
  ——一段**与该字段毫无关系**的内存。同理 `staticRoot + chain[0]` 与
  `ChainSampler` 的 `moduleBase + moduleBaseOffset` 均可回绕。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2；
  `src/gamestate/PointerChainResolver.cpp`、`src/gamestate/ChainSampler.cpp`。
- **根因**：无符号整数加法回绕是**定义良好**的行为（不报错、不崩），
  因此这类缺陷不会以「异常」形式暴露，只会静默返回错误数据——
  恰恰违背 `docs/ROADMAP-ex1.md` §4.3「异常即退回，不伪造」。
- **解决或规避**：新增 `addAddress(base, offset, out, error, what)`，
  以 `base > UINT64_MAX - offset` 判溢出并**明确失败**；
  `resolve()`（静态根 + 每一跳）、`verifyMagic()`（`base + magicOffset`）、
  `ChainSampler::sample()`（`moduleBase + moduleBaseOffset`）全部改用它。
  `readRaw()` 另加「拒绝读空地址」与「预算比较改用减法形式」（`已用 + 请求` 本身也会溢出）。
- **影响与关联文档**：`src/gamestate/**`；
  `tests/test_gamestate_boundaries.cpp` 用「记录每次被读地址」的假读取器
  （`RecordingMemoryReader::reads()`）**断言溢出时一次读取都没发起**，
  而不是只断言返回 false。
