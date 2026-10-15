# P-114 · 学习页用例两处口径：列表选中残留与「外部条目」漏算

| 字段 | 取值 |
|---|---|
| 阶段 | EX8（工作台「学习」栏；`tests/test_study.cpp`） |
| 原编号 | —（EX8 新增） |
| 模块 | `tests/test_study.cpp`（`ui::StudyPanel` 与 `StudyUiPlugin` 的用例） |
| 报错分层 | test（用例侧口径问题，产品行为正确） |
| 严重度 | minor（假失败；修正期望后 14/14 通过） |

## 一、现象（可复现步骤 / 报错原文）

`test_study` 首次运行时失败两条：

```text
FAIL!  : StudyTest::panelImportsAndPreviews() Compared values are not the same
   Actual   (study.items().size()): 0
   Expected (1)                   : 1
F:\develop\desktoppet\tests\test_study.cpp(430) : failure location

FAIL!  : StudyTest::hostJsonIsInjectedAndChangesAreWrittenBack() Compared values are not the same
   Actual   (plugin.service()->items().size())                            : 10
   Expected (static_cast<int>(plugin.service()->availableBlocks().size())): 9
F:\develop\desktoppet\tests\test_study.cpp(483) : failure location
```

## 二、根因

两处都是**用例期望与既定交互语义不符**，产品侧没有缺陷：

1. **选中残留**：学习页的导入交互是「导入后**自动选中最新一条并展示其正文**」（`StudyPanel::onImportSelected`
   里 `setCurrentRow(row)`，列表 current 项**同时带选择**）。用例导入 2 条后只写了
   `importedList()->item(0)->setSelected(true)`，没有先清掉这份自动选择 → 「移出选中」拿到的是**两条**，
   实得 0 条残留；用例却在断言「应剩 1 条」。
2. **漏算外部条目**：该用例先注入了 1 条 `external` 条目（清单 = 1 条外部），随后点「导入全部」把
   9 个功能块全部导入 → 实得 10 条；期望写成 `availableBlocks().size()`（= 9），漏掉了那条外部条目。

## 三、解决或规避

用例侧修正（产品行为一行未改）：

```cpp
// 1) 多选类操作前先清空选择（「导入后自动选中最新一条」是设计行为）
panel.importedList()->clearSelection();
panel.importedList()->item(0)->setSelected(true);
removeSelected->click();
QCOMPARE(study.items().size(), 1);

// 2) 统计期望把「非本次操作产生但合法存在」的条目算进去
QCOMPARE(plugin.service()->items().size(),
         static_cast<int>(plugin.service()->availableBlocks().size()) + 1); // 9 块 + 1 条外部代码
```

**规避口径**：① 断言「移出 / 复制 / 删除选中项」类操作前，**显式清空选择**再指定目标 ——
列表的 current 项（`setCurrentRow` / `setCurrentItem`）在 `ExtendedSelection` 下**同时是选中项**；
② 计数类断言先列清「清单里此刻应当有哪些来源的条目」，别只按本次操作的输入推算。

## 四、影响与关联文档

- 影响面：仅测试；`StudyService` / `StudyPanel` / `StudyUiPlugin` 未改一行。
- 关联：`docs/STUDY.md` §3.2（页面交互与自动选中语义）、`docs/STUDY.md` §6（测试清单）、
  `docs/pitfalls/ex8/P-113-*.md`（同轮另一条实现侧口径缺口），
  同类历史条目 `docs/pitfalls/p11/P-112-*`（用例侧计数期望与真实语义不符）。
