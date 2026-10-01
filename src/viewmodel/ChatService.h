#pragma once

// ChatService（P5）：梗聊天的场景编排层 —— docs/CHAT.md §2–§5、docs/ROADMAP-P5.md。
//
// 职责边界（刻意的窄接口）：
//   - **场景决策**：分时问候 / 心情分层 / 羁绊专属 / 关键词感知，决定「现在该说哪个场景的话」；
//   - **候选选取与最近 N 条去重**委托给 core::LineTable（见 LineTable::pick），
//     本类不重复实现随机与去重；
//   - **节流（≥6s）与深夜静默**由 core::PetStateMachine 统一把关（proactive 台词），
//     本类只负责「是否到了该问候/该报心情的时机」，避免两处计时打架。
//
// 关键词感知的开关 keyword_aware **默认关闭**（CHAT.md §4/§7）；规则常量集中在
// core/ChatRules.h（纯逻辑、零 Qt），本类只做状态去重（同一时段只问候一次、
// 心情/羁绊只在跨档时播报一次）。

#include "core/ChatRules.h"
#include "core/LineTable.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace whalepet::viewmodel {

class ChatService : public QObject {
    Q_OBJECT
public:
    // lines 不接管所有权（PetController 持有同一份；允许为空 → 全部场景降级为不说话）
    explicit ChatService(core::LineTable *lines = nullptr, QObject *parent = nullptr);

    void setLineTable(core::LineTable *lines) { m_lines = lines; }
    core::LineTable *lineTable() const { return m_lines; }

    // ---- 关键词感知 ----
    bool keywordAware() const { return m_keywordAware; }
    void setKeywordAware(bool on) { m_keywordAware = on; }

    // 文本扫描（被动监听，如剪贴板）：开关关闭 → 恒为空串；命中 → 关键词 id（如 "omg"）。
    // 规则与顺序见 core/ChatRules.h：**自定义热词优先**，再按 kKeywordRules（首个命中即停）。
    std::string matchText(const QString &text) const;

    // ---- 自定义热词（P6：用户录入的「词 → 关键词 id」）----
    // 由 View 层从 model::HotwordRepo 读入后注入，本类**不依赖 model 层**。
    void setCustomHotwords(const std::vector<core::CustomHotword> &hotwords);
    const std::vector<core::CustomHotword> &customHotwords() const { return m_hotwords; }

    // 显式录入（全局热键 / 菜单）：**不受 keywordAware 门控**。
    // keyword_aware 只管辖「被动监听输入内容」这件事（隐私考虑，默认关）；
    // 录入是用户主动动作（等同点一下桌宠），故始终匹配，否则关着开关就没法用。
    std::string matchHotword(const QString &text) const;

    // ---- 主动发言时机（返回值即场景 key；空串表示「现在不说」）----
    // 分时问候：同一时段只给一次；深夜（23:00–05:59）返回空串。
    std::string greetScene(int hour);
    // 心情分层：仅在心情档位发生变化（Low / High）时返回专属场景。
    std::string moodSceneFor(int mood);
    // 羁绊专属：仅在羁绊跨过 Lv3 / Lv5 / Lv7 时返回专属场景。
    std::string bondSceneFor(int level);

    // 复位内部去重状态（controller start() 时调用）
    void reset();

private:
    // 场景是否有候选台词；无（语料缺失）则优雅降级为不说话
    bool sceneAvailable(const std::string &scene) const;

    core::LineTable *m_lines = nullptr;
    bool m_keywordAware = false;
    std::vector<core::CustomHotword> m_hotwords; // 优先级高于 kKeywordRules

    int m_lastGreetSlot = -1;  // core::GreetSlot 的整数值；-1 = 尚未问候过
    int m_lastMoodTier = -1;   // -1 = 尚未建立基线（首次观察不发言）
    int m_lastBondTier = -1;   // 同上
};

} // namespace whalepet::viewmodel
