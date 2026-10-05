# 本地 Context API 的 HTTP 通道可被浏览器跨站调用

> **原编号**：`TRAP-P7-013`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤）**：
  1. 用浏览器打开任意第三方页面（无需与 WhalePet 有任何关系）。
  2. 页面内执行：
     ```js
     fetch('http://127.0.0.1:<port>/rpc', {
       method: 'POST',
       headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
       body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'context.snapshot' })
     })
     ```
  3. 服务端**照常执行**并返回 200 与完整 `ContextSnapshot`（含前台窗口标题、
     应用名、空闲时长、会话统计等隐私数据）；对**有副作用**的能力同样可被触发。
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，配置 Debug/Release，
  受影响文件 `src/contextapi/transport/LocalHttpTransport.{h,cpp}`。
- **根因**：两条独立的缺陷叠加。
  1. `handleRequestLine` 里 `if (!m_token.isEmpty()) { …校验… }`——
     **token 为空即完全跳过认证**。而 `context_api_token` 默认为空串，
     于是「开启 Context API」= 「对全世界的网页开放一次 JSON-RPC 工具调用」。
     开发者把「仅监听 127.0.0.1」当成了授权机制，但**回环绑定不是授权**：
     浏览器可以向任意本机端口发请求。
  2. 既不校验 `Origin`、也不限制 `Content-Type`。而 `text/plain` /
     `application/x-www-form-urlencoded` 属 CORS **简单请求**，浏览器**不触发预检**、
     不管服务端是否回了 CORS 头都会把请求发出去（只是读不到响应）——
     对「只写不读」的攻击（如有副作用的工具调用）足够了。
  3. 附带：单连接缓冲、并发连接数、单请求等待时长**均无上限**，
     慢速客户端可长期占用描述符与内存。
- **解决或规避**：改为**三层纵深防御**（`docs/CONTEXT-API.md` §5.1）：
  1. `LocalHttpTransport::start()` 在 `m_token.trimmed().isEmpty()` 时
     **直接返回 false**（fail closed，**根本不监听**）；`ContextApiService::start()`
     在无令牌时只启用命名管道通道（浏览器不可达 + Windows 命名管道 ACL 保护）。
     组合根 `PetWindow::setContextApiEnabled` 在「用户启用但未配置令牌」时用
     `ContextApiService::generateToken()`（256 bit CSPRNG → base64url）生成并落盘，
     避免功能形同虚设。
  2. `Content-Type` 必须 `application/json`（允许 `; charset=utf-8` 等参数与大小写变体），
     否则 `415`——`application/json` 会触发预检，而本通道**从不**回
     `Access-Control-Allow-*`，预检必然失败。
  3. 带 `Origin` 时必须同源同端口，否则 `403`；`Origin: null`、`file://`、userinfo、
     非回环主机、异端口全部拒绝。
  4. 令牌改**定长比较**（`secureEquals`，不因首个不同字节短路），避免计时侧信道。
  5. 补齐资源上限：请求头 16 KiB、正文 1 MiB、并发连接 32、单请求等待 10s
     （250ms 巡检，无需每连接一个定时器），超限即回错误并关闭连接。
- **影响与关联文档**：`docs/CONTEXT-API.md` §5.1 / §5.2、`docs/SETTINGS.md` §2；
  回归守卫 `tests/test_context_http_security.cpp`（11 例，含**有副作用假工具**
  `ext.test.sideEffect` 的「调用次数保持 0」断言）与
  `tests/test_context_dispatch.cpp::httpChannelRefusesToStartWithoutToken`。
  **既有测试必须同步更新**：`serviceIsOffByDefaultAndGatedOnDemand` 与
  `httpChannelAnswersJsonRpc` 原先依赖「无 token 也能起 HTTP」，已按新策略改造
  （并新增「无 token ⇒ 只监听管道」的显式断言），非断言削弱。
