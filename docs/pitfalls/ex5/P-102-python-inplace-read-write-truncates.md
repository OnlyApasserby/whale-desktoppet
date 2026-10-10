# P-102 · Python 一行式「就地读写」先截断后读取 → 4 个源文件被清空

| 字段 | 取值 |
|---|---|
| 原始编号 | —（EX5 新编号） |
| 阶段 | EX5 · 小游戏体验优化（国际象棋难度梯度与棋盘朝向 / 找小猫地图与文案） |
| 模块 | `scripts/`（本地验证工具链，非产品代码） |
| 报错分层 | script |
| 严重度 | major（一度丢失 4 个文件的未提交改动） |
| 状态 | 已解决（全部恢复并经构建 + 全量回归验证） |

## 1. 现象（可复现步骤 / 报错原文）

用一行式 Python 给 4 个文件做「去 BOM + CRLF→LF」规范化（见 `P-101`）：

```powershell
python -c "[open(f,'wb').write(open(f,'rb').read().lstrip(b'\xef\xbb\xbf').replace(b'\r\n',b'\n')) for f in fs]"
```

执行后 4 个文件全部变成 **0 字节**：

```text
src/core/Chess.cpp                  → {"result": "", "hint": "File is empty."}
src/minigame/chess/ChessView.cpp    → {"result": "", "hint": "File is empty."}
src/minigame/kitten/KittenView.h    → {"result": "", "hint": "File is empty."}
tests/test_kitten.cpp               → {"result": "", "hint": "File is empty."}
```

`git status` 显示这 4 个文件为 `M`（本轮改动尚未提交 ⇒ 没有可回退的提交点）。

## 2. 根因

Python 的求值顺序：`open(f, 'wb').write(<参数>)` 会**先**求值函数对象 `open(f,'wb')` ——
这一步就以 `'wb'` 打开并**立即截断文件为 0 字节**；随后才求值参数 `open(f,'rb').read()`，
读到的是已被截断的空内容，于是把「空内容」写回，文件被清空。

即：**读与写共用同一路径时，绝不能把 `open(w)` 写在 `open(r)` 的左侧**。

## 3. 解决或规避

- 恢复：从 git 取原始字节写回（**先读后写**，不经过任何字符串转换）：

  ```python
  data = subprocess.run(["git", "show", "HEAD:" + path], capture_output=True, check=True).stdout
  with open(path, "wb") as handle:
      handle.write(data)
  ```

  （`git checkout -- <path>` 效果等价，但会连本轮的**未提交改动**一起丢弃，
  故本次选择 `git show HEAD:<path>` 逐字节取回，再用编辑工具**逐项重放**本轮改动。）
- 重放后重新构建 + 全量回归确认（Debug / Release 各 **34/34**，含本轮新增守卫；
  重放期间未删除断言、未放宽条件）。
- 预防规则（写入本节作为后续硬约束）：
  1. 读写同一文件必须**先读后写**（`data = read(); open(w).write(data)`）；
  2. 批量改仓库文件一律用编辑工具；必须用脚本时，写「临时文件 + 原子替换」；
  3. 任何脚本化改动前后都执行 `git status --short` + 文件长度自查，异常立即停止。

## 4. 影响与关联文档

- 影响范围：4 个文件（`src/core/Chess.cpp`、`src/minigame/chess/ChessView.cpp`、
  `src/minigame/kitten/KittenView.h`、`tests/test_kitten.cpp`）被清空约 10 分钟，
  已全部恢复且内容与本轮设计一致（逐项重放 + 构建 + 测试验证）。
- 关联：`P-101`（同批的 BOM/CRLF 事故）；`docs/README.md` §六；
  本轮验证记录见 `docs/MINIGAME-INTERFACE.md` §9 变更记录。
