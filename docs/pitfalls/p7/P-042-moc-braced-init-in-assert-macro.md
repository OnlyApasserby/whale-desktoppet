# moc 无法解析断言宏内的花括号初始化列表

> **原编号**：`TRAP-P7-004`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：
  ```
  tests/test_plugin_registry.cpp(184:1): error: missing ')' in macro usage
  MSB8066: “...test_plugin_registry_autogen.vcxproj”的自定义生成已退出，代码为 1
  ```
  发生在 **AutoMoc** 阶段（编译器本身尚未运行），仅 `test_plugin_registry` 失败，其余目标正常。
- **根因**：在 `QVERIFY(...)` 内直接写了 `std::vector<std::pair<QString, PluginOrigin>>{...}`
  这类「花括号初始化列表 + 多层模板」表达式；moc 的轻量预解析对宏参数中的这种写法匹配括号失败。
- **解决或规避**：把复杂表达式移出断言宏——先落到局部变量或经小工具函数（`caps({...})`）构造，
  宏内只保留简单调用（`QVERIFY(added)`）。此约定写入测试文件顶部注释，供后续新增测试沿用。
- **影响与关联文档**：`tests/test_plugin_registry.cpp`；`docs/TESTING.md` 的测试编写约定。

---
