# ---------------------------------------------------------------------------
# 模块：Tests —— 测试目标定义与 CTest 注册
# 职责：enable_testing() 与全部测试可执行目标、测试辅助进程（合成靶进程 /
#       ACP 测试 Agent / MCP 测试服务端等）的定义、依赖注入与 CTest 注册。
# 依赖：Libraries（被测静态库）、Executables（test_context_pipe 依赖 whalepet-mcp）。
# 说明：由顶层通过 include() 引入，与顶层共享同一变量作用域。
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# 测试（Linux 无显示 / CI 无桌面时返回 77 跳过，见 docs/TESTING.md）
# ---------------------------------------------------------------------------
enable_testing()

qt_add_executable(test_smoke tests/test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE whalepet_view Qt6::Test)
# 随包地图目录：校验「场景切换后网格与判定数据一致」（见 kittenSceneChangeRebuildsGrid）
target_compile_definitions(test_smoke PRIVATE
    "WHALEPET_MAPS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/maps\"")
# `-o -,txt` 强制把 QTest 日志写到 stdout：
# Windows 上 QTest 默认日志器在「无控制台」时改走 OutputDebugString，
# 导致 CTest / 管道 / 文件重定向都拿不到用例结果。见 docs/pitfalls/。
add_test(NAME test_smoke COMMAND test_smoke -o -,txt)
set_tests_properties(test_smoke PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

qt_add_executable(test_state_machine tests/test_state_machine.cpp)
target_link_libraries(test_state_machine PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_state_machine COMMAND test_state_machine -o -,txt)
set_tests_properties(test_state_machine PROPERTIES TIMEOUT 60)

# 台词表：解析 + 取用 + 「真实语料覆盖状态机输出场景」的一致性校验
qt_add_executable(test_line_table tests/test_line_table.cpp)
target_link_libraries(test_line_table PRIVATE whalepet_core Qt6::Test)
target_compile_definitions(test_line_table PRIVATE
    "WHALEPET_LINES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/lines\"")
add_test(NAME test_line_table COMMAND test_line_table -o -,txt)
set_tests_properties(test_line_table PROPERTIES TIMEOUT 60)

# 数据层：建表 / 迁移幂等 / 单例往返 / 事务回滚 / 目录三级降级
qt_add_executable(test_database tests/test_database.cpp)
target_link_libraries(test_database PRIVATE whalepet_model Qt6::Test)
add_test(NAME test_database COMMAND test_database -o -,txt)
set_tests_properties(test_database PROPERTIES TIMEOUT 60)

# 养成：升级曲线 / 增量表 / 夹取 / 饱食衰减 / 跨天签到 / 升级信号 / 持久化
qt_add_executable(test_growth tests/test_growth.cpp)
target_link_libraries(test_growth PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_growth COMMAND test_growth -o -,txt)
set_tests_properties(test_growth PROPERTIES TIMEOUT 60)

# P4 内容层：39 项成就判定 / 每日任务抽签与领取 / 周签到里程碑 / 日记去重与上限
qt_add_executable(test_content tests/test_content.cpp)
target_link_libraries(test_content PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_content COMMAND test_content -o -,txt)
set_tests_properties(test_content PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# P5 聊天：分时问候 / 深夜静默 / 心情分层 / 羁绊专属 / 关键词映射与开关 / 语料覆盖
qt_add_executable(test_chat tests/test_chat.cpp)
target_link_libraries(test_chat PRIVATE whalepet_view Qt6::Test)
target_compile_definitions(test_chat PRIVATE
    "WHALEPET_LINES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/lines\"")
add_test(NAME test_chat COMMAND test_chat -o -,txt)
set_tests_properties(test_chat PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# P6 热词：自定义热词优先匹配 / 归一化与去重 / 热词表 CRUD / 显式录入不受开关门控
# 链 whalepet_view：热词匹配既走 core::matchKeyword，也走 viewmodel::ChatService
# （显式录入 vs 被动监听），同 test_chat。
qt_add_executable(test_hotword tests/test_hotword.cpp)
target_link_libraries(test_hotword PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_hotword COMMAND test_hotword -o -,txt)
set_tests_properties(test_hotword PROPERTIES TIMEOUT 60)

# P6 设置项「生效」：深夜静默开关 / 立绘尺寸与粒子·惯性开关 / 气泡开关
qt_add_executable(test_settings tests/test_settings.cpp)
target_link_libraries(test_settings PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_settings COMMAND test_settings -o -,txt)
set_tests_properties(test_settings PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# 小游戏：扫雷纯逻辑（预设与自定义校验 / 首点安全布雷 / 翻格·插旗 / 胜负与连翻）
qt_add_executable(test_minesweeper tests/test_minesweeper.cpp)
target_link_libraries(test_minesweeper PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_minesweeper COMMAND test_minesweeper -o -,txt)
set_tests_properties(test_minesweeper PROPERTIES TIMEOUT 60)

# 小游戏：鲸鱼娘找小猫纯逻辑（物体表与地图解析 / 移动与撞墙 / 物体交互与消费 / 场景切换 /
# 通关判定与结算折算）
qt_add_executable(test_kitten tests/test_kitten.cpp)
target_link_libraries(test_kitten PRIVATE whalepet_core Qt6::Test)
# 随包资源目录：校验真实地图可达性与台词覆盖（与运行期 qrc 同源文件）
target_compile_definitions(test_kitten PRIVATE
    "WHALEPET_MAPS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/maps\""
    "WHALEPET_LINES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/lines\"")
add_test(NAME test_kitten COMMAND test_kitten -o -,txt)
set_tests_properties(test_kitten PROPERTIES TIMEOUT 60)

# 小游戏：国际象棋纯逻辑（FEN 往返 / 合法着法 / 王车易位 / 过路兵 / 升变 / 将军将死逼和 /
# UCI 着法串 / 结算折算与难度表）
qt_add_executable(test_chess tests/test_chess.cpp)
target_link_libraries(test_chess PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_chess COMMAND test_chess -o -,txt)
set_tests_properties(test_chess PROPERTIES TIMEOUT 60)

# 小游戏：接 Token 纯逻辑（预设与常量 / 新局与「首次输入才开局」/ 生成与下落接取 /
# 漏接与连击 / 漏接白饭无害 / 接白饭立即结束与危险态 / 达成目标通关与 perfect /
# 时限三档判定 / 接取区移动夹取 / 结算折算 / 随包台词与结束立绘自洽）
qt_add_executable(test_tokencatch tests/test_tokencatch.cpp)
target_link_libraries(test_tokencatch PRIVATE whalepet_core Qt6::Test)
# 随包语料目录：校验 assets/lines/tokencatch.txt 覆盖视图用到的全部场景 key
target_compile_definitions(test_tokencatch PRIVATE
    "WHALEPET_LINES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/lines\"")
add_test(NAME test_tokencatch COMMAND test_tokencatch -o -,txt)
set_tests_properties(test_tokencatch PROPERTIES TIMEOUT 60)

# 立绘资源与加载策略（路径 A）：93 张全部通过「格式白名单 + 尺寸必须 256x256」/
# 闸门拒绝用例（尺寸 / 格式 / 缺失 / 损坏）/ 预载分档一致性 / LRU 容量与逐出 /
# 负缓存不重复解码 / 常驻内存 ≤ M3 目标
qt_add_executable(test_pose_assets tests/test_pose_assets.cpp)
target_link_libraries(test_pose_assets PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_pose_assets COMMAND test_pose_assets -o -,txt)
set_tests_properties(test_pose_assets PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# 回收站清理提醒（立绘激活 18）：查询不崩 / 启停切换 / start 立即检查一次
qt_add_executable(test_recyclebin tests/test_recyclebin.cpp)
target_link_libraries(test_recyclebin PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_recyclebin COMMAND test_recyclebin -o -,txt)
set_tests_properties(test_recyclebin PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# 小游戏结算：档位奖励 / 每日上限（每日 3 局）/ 个人最快与跨天清零 / 落库往返
qt_add_executable(test_minigame tests/test_minigame.cpp)
target_link_libraries(test_minigame PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_minigame COMMAND test_minigame -o -,txt)
set_tests_properties(test_minigame PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# P7：插件化智能桌宠与本地 Context API（docs/ROADMAP-P7-Fin.md P7.0）
# ---------------------------------------------------------------------------

# 能力总线：插件注册 / 能力收集 / 冲突仲裁（Builtin > Dll > Process）/ 同步与异步调用契约 /
# 生命周期容错 / 内置层装载器 / 小游戏兼容适配（minigame.* 能力）
qt_add_executable(test_plugin_registry tests/test_plugin_registry.cpp)
target_link_libraries(test_plugin_registry PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_plugin_registry COMMAND test_plugin_registry -o -,txt)
set_tests_properties(test_plugin_registry PROPERTIES TIMEOUT 60)

# 感知层骨架：空实现恒「无数据」/ 组合观察者的类别·切换·停留·滚动窗口 / 失败不伪造数据
qt_add_executable(test_platform_skeleton tests/test_platform_skeleton.cpp)
target_link_libraries(test_platform_skeleton PRIVATE whalepet_platform Qt6::Test)
add_test(NAME test_platform_skeleton COMMAND test_platform_skeleton -o -,txt)
set_tests_properties(test_platform_skeleton PROPERTIES TIMEOUT 60)

# P7.1 真实 Win32 感知：文本工具（宽字符→UTF-8 / 路径取文件名）、三个子采样器的
# 「注入替身读数」行为（含钩子不可用时的差分降级、失败不伪造）、会话锁定→Afk 端到端。
# 全部用注入替身驱动，**不安装任何系统钩子**、不依赖真实前台窗口（可在 CI/headless 下稳定运行）。
if(WIN32)
    qt_add_executable(test_win32_observer tests/test_win32_observer.cpp)
    target_link_libraries(test_win32_observer PRIVATE whalepet_platform Qt6::Test)
    add_test(NAME test_win32_observer COMMAND test_win32_observer -o -,txt)
    set_tests_properties(test_win32_observer PROPERTIES TIMEOUT 60)
endif()

# 工作状态：应用类别归一化 / 各状态判据（含 Coding vs Vibe Coding）/ 置信度与滞回 /
# 状态机工作态通道（专注态静默与 work.* 豁免、不打断一次性表现、Unknown 零回归）
qt_add_executable(test_work_state tests/test_work_state.cpp)
target_link_libraries(test_work_state PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_work_state COMMAND test_work_state -o -,txt)
set_tests_properties(test_work_state PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# P8：时段常驻立绘 / 工作立绘池 / 预设对话 / 天气判定（docs/ROADMAP-P8.md）
# ---------------------------------------------------------------------------

# 时段划分（日间 07–17 / 傍晚 18–22 / 深夜 23–06）与三档常驻立绘 /
# 彩云中文天气描述 → 天气类型（优先级：雷 > 雹 > 雪 > 雨 > 雾霾尘 > 阴 > 云 > 晴）/
# 工作立绘池轮转与「最近」避重（热词 / ACP 联动）/ 预设对话语料解析与问题池
# （天气题为保留项、QA 后刷新池、三回答随机）
qt_add_executable(test_preset_dialogue tests/test_preset_dialogue.cpp)
target_link_libraries(test_preset_dialogue PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_preset_dialogue COMMAND test_preset_dialogue -o -,txt)
set_tests_properties(test_preset_dialogue PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# P10：代码彩蛋（迁移后只剩话池 + 注释渲染）与工作台内容层
# ---------------------------------------------------------------------------
# 代码彩蛋：话池安全性（无换行 / 无块注释闭合符 / 不以反斜杠结尾 / 无裸引号）+
# eggCommentLine 的幂等标记与空值语义。（改写用户源码的旧机制已随 P10 退役。）
qt_add_executable(test_code_easter_egg tests/test_code_easter_egg.cpp)
target_link_libraries(test_code_easter_egg PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_code_easter_egg COMMAND test_code_easter_egg -o -,txt)
set_tests_properties(test_code_easter_egg PROPERTIES TIMEOUT 60)

# 工作台内容层（core::PythonBlocks + viewmodel::WorkService）：
#   索引解析 / 组装（头注释 + 逐块**原样** + 末尾分发器）/ 彩蛋注释插入 /
#   资源契约（每个索引条目都有正文且定义 run_<entry>()）/ 会话产出（原子写、绝不覆盖、
#   目录不可用即拒绝、戳一戳每次会话至多一次）。
# 约定同其它语料类用例：用**磁盘目录**读资源（WHALEPET_BLOCKS_DIR），不依赖 qrc 初始化。
qt_add_executable(test_python_blocks tests/test_python_blocks.cpp)
target_link_libraries(test_python_blocks PRIVATE whalepet_view Qt6::Test)
target_compile_definitions(test_python_blocks PRIVATE
    "WHALEPET_BLOCKS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/python/blocks\"")
add_test(NAME test_python_blocks COMMAND test_python_blocks -o -,txt)
set_tests_properties(test_python_blocks PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# 【EX3 已移除】EX1 外部游戏陪玩的测试目标与合成靶进程
#   —— game_target_sim / test_game_memory / test_game_memory_e2e /
#      test_unity_adapters / test_rpgmaker_adapters 已移至 dump/tests（不入库）。
#   —— 保留 test_game_companion（判定规则 / 状态机通道 / 服务编排，属复用管线）。
# ---------------------------------------------------------------------------

# EX1.4 游戏陪玩：判定规则（血量→持续态 / 置信度与滞回 / 里程碑边沿 / 立绘场景映射）/
# 状态机游戏态通道（最低让位优先级 / 不打断一次性姿态 / 里程碑播报 / 静默陪伴 / 零回归）/
# GameCompanionService 采样调度（启停 / 上报 / 危险与里程碑透传 / 适配器失效自动停用）
qt_add_executable(test_game_companion tests/test_game_companion.cpp)
target_link_libraries(test_game_companion PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_game_companion COMMAND test_game_companion -o -,txt)
set_tests_properties(test_game_companion PROPERTIES TIMEOUT 60)

# EX4 小游戏陪玩：中立契约与中立判定（GameSnapshot / MiniGameCompanion：mood / 里程碑边沿 /
# 置信度与滞回 / 折算管线）+ 陪玩侧通用聚合（MiniGameCompanionSource：候选选取 / 不伪造 /
# 与具体玩法无关：任意新「游戏」只要实现 IMiniGameCompanionSource 即被接入）
qt_add_executable(test_minigame_companion tests/test_minigame_companion.cpp)
target_link_libraries(test_minigame_companion PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_minigame_companion COMMAND test_minigame_companion -o -,txt)
set_tests_properties(test_minigame_companion PROPERTIES TIMEOUT 60)

# Context API：JSON-RPC 校验与错误码 / 能力别名路由 / 门控与回环绑定 /
# 双通道（本机 HTTP + MCP stdio）共用同一 dispatcher 与能力表
qt_add_executable(test_context_dispatch tests/test_context_dispatch.cpp)
target_link_libraries(test_context_dispatch PRIVATE whalepet_contextapi Qt6::Test)
add_test(NAME test_context_dispatch COMMAND test_context_dispatch -o -,txt)
set_tests_properties(test_context_dispatch PROPERTIES TIMEOUT 120)

# P7.2 通道启用与 MCP 桥接：命名管道承载完整 MCP 会话 / token 门控 / 总开关同时启停两通道 /
# 以及**真实桥接进程 whalepet-mcp.exe 端到端**（stdio ↔ 命名管道 ↔ 宿主，含 --token 注入）
qt_add_executable(test_context_pipe tests/test_context_pipe.cpp)
target_link_libraries(test_context_pipe PRIVATE whalepet_contextapi Qt6::Test)
target_compile_definitions(test_context_pipe PRIVATE
    "WHALEPET_MCP_EXE=\"$<TARGET_FILE:whalepet-mcp>\"")
add_dependencies(test_context_pipe whalepet-mcp)
add_test(NAME test_context_pipe COMMAND test_context_pipe -o -,txt)
set_tests_properties(test_context_pipe PROPERTIES TIMEOUT 120)

# 【EX3 已移除】test_gamestate_boundaries（gamestate 只读链路的资源与数值边界）
#   —— 已移至 dump/tests（不入库）。

# SECURITY-REVIEW.md 极端边界 1/2：HTTP 通道跨站调用与认证（空 token fail closed /
# Origin 同源校验 / Content-Type 限制 / 响应不带 CORS 头）、请求缓冲上限与连接清理
# （超长头 / 超大与冲突 Content-Length / chunked / 未结束正文 / 慢速客户端超时 / 反复启停）
qt_add_executable(test_context_http_security tests/test_context_http_security.cpp)
target_link_libraries(test_context_http_security PRIVATE whalepet_contextapi Qt6::Test)
add_test(NAME test_context_http_security COMMAND test_context_http_security -o -,txt)
set_tests_properties(test_context_http_security PROPERTIES TIMEOUT 180)

# P7.5 ACP / IDE 显式信号：JSONL 信号源增量读取 / 非法行忽略 / 未换行尾部 / 截断重置；
# Agent 会话桥接（生命周期幂等 + 事件落盘）；信号→工作态映射；显式信号覆盖推断与过期回落
qt_add_executable(test_acp tests/test_acp.cpp)
target_link_libraries(test_acp PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_acp COMMAND test_acp -o -,txt)
set_tests_properties(test_acp PROPERTIES TIMEOUT 60)

# P7.6 ACP 事件映射：以**真实 dsh 报文**夹具（`dsh --profile acp` 实测采集）驱动
# AcpEventMapper（ACP session/update → CoreSignal）与 AcpSignalRules（→ 工作态）
qt_add_executable(test_acp_event_mapper tests/test_acp_event_mapper.cpp)
target_link_libraries(test_acp_event_mapper PRIVATE whalepet_contextapi Qt6::Test)
target_compile_definitions(test_acp_event_mapper PRIVATE
    "WHALEPET_ACP_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/acp-real-events.json\"")
add_test(NAME test_acp_event_mapper COMMAND test_acp_event_mapper -o -,txt)
set_tests_properties(test_acp_event_mapper PROPERTIES TIMEOUT 60)

# P7.6 ACP 客户端：以一个真实子进程（acp_test_agent）端到端验证
# 启动 + initialize 握手 / session/new / session/list + session/resume /
# session/prompt 事件映射 / 权限自动应答 / Agent 崩溃隔离。
# `realDshSmokeOrSkip` 在设置环境变量 WHALEPET_ACP_REAL_DSH=<dsh>/lib/bin.js 时
# 会用**真实 DeepSeek Harness**（`dsh --profile acp`）跑一遍，否则跳过（CI 友好）。
qt_add_executable(acp_test_agent tests/acp_test_agent.cpp)
target_link_libraries(acp_test_agent PRIVATE Qt6::Core)

qt_add_executable(test_acp_client tests/test_acp_client.cpp)
target_link_libraries(test_acp_client PRIVATE whalepet_contextapi Qt6::Test)
add_dependencies(test_acp_client acp_test_agent)
add_test(NAME test_acp_client COMMAND test_acp_client -o -,txt)
set_tests_properties(test_acp_client PROPERTIES TIMEOUT 120)

# P7.4 外部进程插件（MCP Client）：以一个真实子进程（mcp_test_server）端到端验证
# 配置校验 / initialize 握手 + tools/list 能力发现 / tools/call 异步转发 /
# 工具错误透传 / 调用超时 / 子进程崩溃隔离（pending 回投 + 能力标记不可用）
qt_add_executable(mcp_test_server tests/mcp_test_server.cpp)
target_link_libraries(mcp_test_server PRIVATE Qt6::Core)

qt_add_executable(test_process_plugin tests/test_process_plugin.cpp)
target_link_libraries(test_process_plugin PRIVATE whalepet_plugin Qt6::Test)
add_dependencies(test_process_plugin mcp_test_server)
add_test(NAME test_process_plugin COMMAND test_process_plugin -o -,txt)
set_tests_properties(test_process_plugin PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# P9-A：宿主服务经 builtin 层注册化（docs/ROADMAP-P9-Fin.md §P9-A）
# ---------------------------------------------------------------------------
# 5 个宿主服务以 IPlugin 形式注册进能力总线 / 各暴露 1 个只读状态能力
# （id 与 builtinServiceCapabilityIds() 一致）/ 未启动时能力返回「不可用」（不伪造数据）/
# startAll 回填服务句柄；缺 controller 时对话插件优雅降级（不崩溃）。
qt_add_executable(test_service_plugins tests/test_service_plugins.cpp)
target_link_libraries(test_service_plugins PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_service_plugins COMMAND test_service_plugins -o -,txt)
set_tests_properties(test_service_plugins PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# EX6：随机闪现（docs/PRESENTATION.md §3.2）
# ---------------------------------------------------------------------------
# core/RandomFlickerRules 纯逻辑（安全矩形内缩 / 落点恒在安全矩形内 / 最小位移 /
# 全部太近时退化为对角镜像 / 空随机源与过小屏幕降级）+ FlickerService 调度
# （随机间隔区间与可复现 / 启停幂等 / 门槛不满足不发射 / flickerNow 立即发射）。
# 纯逻辑 + QObject 服务：不构造 Widget，无需 offscreen。
qt_add_executable(test_flicker tests/test_flicker.cpp)
target_link_libraries(test_flicker PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_flicker COMMAND test_flicker -o -,txt)
set_tests_properties(test_flicker PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# P9-C：UI 宿主契约与贡献点协议（docs/ROADMAP-P9-Fin.md §P9-C）
# ---------------------------------------------------------------------------
# 贡献点收集（order 升序 / 同序保持注册顺序 / 去重 / 空 id 跳过）/ IPluginUiHost 上下文
# （父窗口 / 生命周期回调 / 面板展示）/ 按 kind 分发到右键、托盘与设置页（createView 延迟创建、
# checkable 写回）/ 试点 StatusPanelUiPlugin（右键 + 托盘两项、展示前刷新、签到回传）。
qt_add_executable(test_ui_plugin_host tests/test_ui_plugin_host.cpp)
target_link_libraries(test_ui_plugin_host PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_ui_plugin_host COMMAND test_ui_plugin_host -o -,txt)
set_tests_properties(test_ui_plugin_host PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# P11：绘画模块（docs/DRAWING.md）
# ---------------------------------------------------------------------------
#   * core/DrawingPalette：完全随机 RGB 口径 / 钳制 / 十六进制 / 感知亮度；
#   * core/AbstractArt：抽象画笔触的范围 / 确定性 / 批次与上限；
#   * core/LineArt：拓扑固定 + 拼接关系（头插入躯干 / 鳍贴躯干角点 / 尾切躯干右边界 /
#     眼在头内 / 水柱接头顶）+ 风格共用 + 同种子可复现；
#   * core/CanvasBoard：笔触模型语义（开笔 / 加点 / 收笔 / 撤销 / 清空 / 越界钳制 / 包围盒）；
#   * 工作台多页化：WorkbenchPage 贡献点经 UiContributionHost 通用分发成页签（默认参数
#     仍保持 SettingsTab 原行为）；
#   * 绘画插件与页面：三个贡献点与顺序、懒建幂等、抽象画连续刷新与暂停、线条画可注入计划、
#     画板取色回填与「最近取色」列表；
#   * 屏幕取色覆盖层：**注入替身**下走完取色 → 确认 / 右键 / ESC 取消 / 采样失败保底，
#     全程不触碰真实屏幕、不创建原生放大镜。
qt_add_executable(test_drawing tests/test_drawing.cpp)
target_link_libraries(test_drawing PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_drawing COMMAND test_drawing -o -,txt)
set_tests_properties(test_drawing PROPERTIES TIMEOUT 120)

# ---------------------------------------------------------------------------
# EX8：工作台「学习」栏（docs/STUDY.md）
# ---------------------------------------------------------------------------
#   * core::PythonImports —— import 归位（仅顶层 / 单行完整语句；归位到注释头之后）与去重
#     （按「去注释 + 折叠空白」比较，保留首次出现）+ 无 import 零改动 + 幂等；
#   * viewmodel::StudyService —— 手动导入功能块（未知 id / 重复跳过）/ 外部代码条目 /
#     只读阅读（功能块实时读源）/ 拼接预览（按导入顺序 + 既有组装口径 + import 归位去重）/
#     失效源处理 / JSON 往返（study_imported）；
#   * viewmodel::WorkService::blockBody —— 按 id 读正文（非法 id 与缺失返回空）；
#   * ui::StudyPanel / ui::StudyUiPlugin —— WorkbenchPage 贡献点（标题 / 顺序 / 懒建幂等）、
#     页内导入与移出、拼接按钮、宿主清单注入与清单变化回写（Hooks）。
# 约定同工作台内容层用例：用**磁盘目录**读资源（WHALEPET_BLOCKS_DIR），不依赖 qrc 初始化。
qt_add_executable(test_study tests/test_study.cpp)
target_link_libraries(test_study PRIVATE whalepet_view Qt6::Test)
target_compile_definitions(test_study PRIVATE
    "WHALEPET_BLOCKS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/python/blocks\"")
add_test(NAME test_study COMMAND test_study -o -,txt)
set_tests_properties(test_study PROPERTIES TIMEOUT 60)
