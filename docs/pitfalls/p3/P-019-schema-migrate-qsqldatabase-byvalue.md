# `Schema::migrate()` 无法接收按值返回的 `QSqlDatabase`

> **原编号**：`TRAP-P3-002`　**阶段**：P3　**来源**：原按阶段聚合的 `traps-P3.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：编译错误 ｜ **影响**：`test_database` 无法编译，构建中断

### 现象

`cmake --build build --config Debug` 报错原文：

```
F:\develop\desktoppet\tests\test_database.cpp(114,9): error C2664: “bool whalepet::model::Schema::migrate(QSqlDatabase
&)”: 无法将参数 1 从“QSqlDatabase”转换为“QSqlDatabase &” [F:\develop\desktoppet\build\test_database.vcxproj]
```

### 根因

`Database::db()` 的签名是**按值返回**（与 Qt 惯例一致，`QSqlDatabase` 是轻量句柄）：

```cpp
QSqlDatabase db() const { return m_db; }
```

而 `Schema::migrate()` 需要非 const 左值引用（内部要执行 `QSqlQuery`）。
`Schema::migrate(db.db())` 传进去的是**临时对象**，无法绑定到非 const 左值引用。

### 解决

在调用点先取出具名副本再传入（也顺带明确了「迁移操作的是一个连接句柄」这一语义）：

```cpp
QSqlDatabase handle = db.db();
QVERIFY(Schema::migrate(handle));
```

`Database::schemaVersion()` 内部早有同样的写法（`QSqlDatabase handle = m_db;`），保持一致。

> 备选方案是把 `Database::db()` 改为返回 `QSqlDatabase&`，但那会暴露内部成员、
> 让调用方有机会在连接被 `close()` 后继续持有一只「半死」句柄，故未采用。

### 影响与关联文档

- 新增测试代码时，凡需要把连接传给 `Schema` / `QSqlQuery` 的地方，一律先取具名副本。
- `Database::close()` 已按「先释放所有 `QSqlQuery` 引用再 `removeDatabase()`」的顺序实现，
  避免 `QSqlDatabase: connection is still in use` 警告（见 `Database.cpp`）。

---
