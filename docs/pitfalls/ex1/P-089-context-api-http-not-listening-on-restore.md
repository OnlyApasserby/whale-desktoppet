# Context API 随设置持久化启用时 HTTP 通道不监听（`start()` 非幂等 + 原子回滚）

> **原编号**：`TRAP-EX1-010`　**阶段**：EX1（EX1.5 真机验收期间发现；缺陷本体属 Context API / P7.2 生命周期）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/pitfalls/index.md`。

---

- **现象（可复现步骤 / 报错原文）**：
  1. 用 `settings.json_ext` 持久化启用 Context API（`context_api_enabled=true`、`context_api_port=45911`、
     `context_api_token=<非空>`），启动 `deploy-release/WhalePet.exe`。
  2. 观测：命名管道已监听（`\\.\pipe\whalepet-context-v1` 存在，经 `whalepet-mcp.exe` 可完成
     `initialize` + `tools/call`），但 **HTTP 端口完全没有监听**
     （`Get-NetTCPConnection -LocalPort 45911` 为空、`netstat` 无该端口、`POST http://127.0.0.1:45911/rpc`
     原文报「无法连接到远程服务器」）。
  3. 会话内无任何 UI 操作（纯「随设置恢复」路径）。
- **根因（已定位）**：`PetWindow::applyWorkStateSettings()`（`src/view/PetWindow.cpp:1093-1121`）的**执行顺序**：
  1. 行 1099-1101 `m_contextApiAction->setChecked(true)` —— `QAction::setChecked` **触发 `toggled`**，
     直连 `setContextApiEnabled(true)`；此刻 **token 尚未写入**（`setToken` 在行 1111），
     于是 `ContextApiService::start()` 走 `httpEnabled=false`，**只启动命名管道**（成功）。
  2. 行 1109-1112 才 `setHttpPort()` / `setToken()`。
  3. 行 1119 再次 `setContextApiEnabled(true)` → **第二次** `start()`；此时 `httpEnabled=true`，
     HTTP 正常 `listen(45911)`，但紧接着 `LocalPipeTransport::start()` 在**已监听**时
     直接返回 `false`（`src/contextapi/transport/LocalPipeTransport.cpp:23-26`：
     `"本地命名管道通道已在监听"`）→ 触发 `ContextApiService::start()` 的**原子回滚**
     （`src/contextapi/ContextApiService.cpp:125-133`：`m_http->stop()` 后 `return false`）
     → **HTTP 被自己关掉**，`start()` 返回 false（日志 `[PetWindow] 本地 Context API 启动失败: …`）。
  4. 净效果：管道在听、HTTP 不听、`running()` 仍为 true（半启动）。
- **解决或规避（✅ 已修复 2026-10-07，最小改动两处）**：
  1. **顺序修正**：`PetWindow::applyWorkStateSettings` 把 `setHttpPort()` / `setToken()` **上移**到
     菜单勾选态同步之前，保证 `setChecked` 触发的那次 `start()` 已带上端口与令牌；
  2. **幂等化**：`ContextApiService::start()` 在启动前先 `m_pipe->stop()` / `m_http->stop()` 收拢既有通道，
     使「重复 start / 改配置后 start」都从干净状态开始，不再因命名管道「已在监听」而回滚 HTTP。
  - **验证（可观测）**：新增回归用例 `test_context_pipe::serviceStartIsIdempotent`——连续两次 `start()`
    后 `pipeListening()==true` 且 `httpPort()!=0`（修复前第二次 `start()` 返回 false 且 HTTP 被回滚）；
    Debug / Release 各 **7/7** 受影响测试通过。
  - **真机复验**：以持久化设置（`context_api_enabled=true`、端口 45911）启动 WhalePet 后，
    `Get-NetTCPConnection -LocalPort 45911` 显示 `127.0.0.1:45911 Listen`（修复前完全不监听），
    且 HTTP `POST /rpc` 可用。
- **影响与关联文档**：`src/view/PetWindow.cpp:1093-1121` / `:1159-1202`、`src/contextapi/ContextApiService.cpp:92-145`、
  `src/contextapi/transport/LocalPipeTransport.cpp:21-48`、`docs/CONTEXT-API.md` §4/§5.1（「总开关原子性」）；
  关联 `docs/ROADMAP-ex1.md` EX1.5（本轮端到端观测因此改走**命名管道**而非 HTTP）。
  **未影响**：命名管道通道与上下文数据本身正常（`context.snapshot` 取值正确）。
